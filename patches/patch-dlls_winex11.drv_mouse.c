--- dlls/winex11.drv/mouse.c.orig
+++ dlls/winex11.drv/mouse.c
@@ -27,6 +27,13 @@
 
 #include <math.h>
 #include <dlfcn.h>
+#include <fcntl.h>
+#include <pthread.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <sys/mman.h>
+#include <time.h>
+#include <unistd.h>
 #include <X11/Xlib.h>
 #include <X11/cursorfont.h>
 #include <stdarg.h>
@@ -66,6 +73,91 @@
 
 WINE_DEFAULT_DEBUG_CHANNEL(cursor);
 
+/***********************************************************************
+ *		mouse diagnostics ring
+ *
+ * With WINE_MOUSE_DIAG=<dir> set, every process keeps the last
+ * MOUSE_DIAG_COUNT pointer events in <dir>/mouse-<pid>.ring, a file mapped
+ * MAP_SHARED so it can be read while the process runs (or after it dies).
+ * Logging is a few stores per event, no system calls.
+ */
+#define MOUSE_DIAG_MAGIC 0x31474149444d5857ull  /* "WXMDIAG1" */
+#define MOUSE_DIAG_COUNT (1u << 18)
+
+struct mouse_diag_record
+{
+    unsigned long long time_us;  /* CLOCK_MONOTONIC */
+    unsigned int tid;
+    unsigned short type;
+    unsigned short flags;
+    int a, b, c, d;
+};
+
+struct mouse_diag_header
+{
+    unsigned long long magic;
+    unsigned int count;
+    unsigned int record_size;
+    unsigned long long next;     /* total records written */
+    long long realtime_offset_us;  /* CLOCK_REALTIME - CLOCK_MONOTONIC at start */
+    int pid;
+    int pad[7];
+};
+
+static struct mouse_diag_header *mouse_diag_header;
+static struct mouse_diag_record *mouse_diag_records;
+static pthread_once_t mouse_diag_once = PTHREAD_ONCE_INIT;
+
+static unsigned long long mouse_diag_now( clockid_t clock )
+{
+    struct timespec ts;
+    clock_gettime( clock, &ts );
+    return ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
+}
+
+static void mouse_diag_init(void)
+{
+    size_t size = sizeof(struct mouse_diag_header) + MOUSE_DIAG_COUNT * sizeof(struct mouse_diag_record);
+    const char *dir = getenv( "WINE_MOUSE_DIAG" );
+    char path[4096];
+    void *map;
+    int fd;
+
+    if (!dir || !*dir) return;
+    snprintf( path, sizeof(path), "%s/mouse-%d.ring", dir, (int)getpid() );
+    if ((fd = open( path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644 )) < 0) return;
+    if (ftruncate( fd, size ) < 0) { close( fd ); return; }
+    map = mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
+    close( fd );
+    if (map == MAP_FAILED) return;
+
+    mouse_diag_header = map;
+    mouse_diag_records = (struct mouse_diag_record *)(mouse_diag_header + 1);
+    mouse_diag_header->count = MOUSE_DIAG_COUNT;
+    mouse_diag_header->record_size = sizeof(struct mouse_diag_record);
+    mouse_diag_header->realtime_offset_us = mouse_diag_now( CLOCK_REALTIME ) - mouse_diag_now( CLOCK_MONOTONIC );
+    mouse_diag_header->pid = getpid();
+    __atomic_store_n( &mouse_diag_header->magic, MOUSE_DIAG_MAGIC, __ATOMIC_RELEASE );
+}
+
+void mouse_diag( unsigned short type, unsigned short flags, int a, int b, int c, int d )
+{
+    struct mouse_diag_record *rec;
+
+    pthread_once( &mouse_diag_once, mouse_diag_init );
+    if (!mouse_diag_header) return;
+
+    rec = &mouse_diag_records[__atomic_fetch_add( &mouse_diag_header->next, 1, __ATOMIC_RELAXED ) % MOUSE_DIAG_COUNT];
+    rec->time_us = mouse_diag_now( CLOCK_MONOTONIC );
+    rec->tid = GetCurrentThreadId();
+    rec->type = type;
+    rec->flags = flags;
+    rec->a = a;
+    rec->b = b;
+    rec->c = c;
+    rec->d = d;
+}
+
 /**********************************************************************/
 
 #ifndef Button6Mask
@@ -434,7 +526,40 @@
 }
 
 #endif /* HAVE_X11_EXTENSIONS_XINPUT2_H */
+
+/* touch contacts that have seen XI_TouchBegin but not XI_TouchEnd */
+#define MAX_ACTIVE_TOUCHES 16
+static struct active_touch
+{
+    unsigned int id;
+    HWND hwnd;
+    LPARAM pos;
+} active_touches[MAX_ACTIVE_TOUCHES];
+static int active_touch_count;
+
+/***********************************************************************
+ *		end_active_touches
+ *
+ * Once the pointer is grabbed (or focus leaves), the X server stops sending
+ * us XI_TouchEnd for touches already in progress, and win32u would keep the
+ * contacts down forever. Release them ourselves.
+ */
+void end_active_touches(void)
+{
+    while (active_touch_count)
+    {
+        struct active_touch *touch = &active_touches[--active_touch_count];
+        INPUT input = {.type = INPUT_HARDWARE};
 
+        TRACE( "ending touch %u\n", touch->id );
+        mouse_diag( MDIAG_TOUCH, 3, touch->id, (short)LOWORD(touch->pos), (short)HIWORD(touch->pos), 0 );
+        input.hi.uMsg = WM_POINTERUP;
+        input.hi.wParamL = touch->id;
+        input.hi.wParamH = POINTER_MESSAGE_FLAG_INRANGE | POINTER_MESSAGE_FLAG_INCONTACT;
+        NtUserSendHardwareInput( touch->hwnd, 0, &input, touch->pos );
+    }
+}
+
 /***********************************************************************
  *		grab_clipping_window
  *
@@ -459,6 +584,7 @@
     if (keyboard_grabbed)
     {
         WARN( "refusing to clip to %s\n", wine_dbgstr_rect(clip) );
+        mouse_diag( MDIAG_CLIP, 2, clip->left, clip->top, clip->right, clip->bottom );
         return FALSE;
     }
     if (!xinput2_available)
@@ -487,6 +613,7 @@
         clip->right < clip_rect.right || clip->bottom < clip_rect.bottom)
         data->warp_serial = NextRequest( data->display );
 
+    end_active_touches();
     if (!XGrabPointer( data->display, clip_window, False,
                        PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                        GrabModeAsync, GrabModeAsync, clip_window, None, CurrentTime ))
@@ -503,6 +630,8 @@
 
     set_window_cursor( clip_window, cursor );
 
+    mouse_diag( MDIAG_CLIP, clipping_cursor | (keyboard_grabbed << 1),
+                clip->left, clip->top, clip->right, clip->bottom );
     if (!clipping_cursor) return FALSE;
     clip_rect = *clip;
     data->clipping_cursor = TRUE;
@@ -526,6 +655,8 @@
     if (!clip_window) return;
 
     TRACE( "no longer clipping\n" );
+    mouse_diag( MDIAG_UNCLIP, clipping_cursor, 0, 0, 0, 0 );
+    end_active_touches();
     XUnmapWindow( data->display, clip_window );
     if (clipping_cursor) XUngrabPointer( data->display, CurrentTime );
     clipping_cursor = FALSE;
@@ -1435,6 +1566,7 @@
     if (keyboard_grabbed)
     {
         WARN( "refusing to warp to %u, %u\n", pos.x, pos.y );
+        mouse_diag( MDIAG_SET_POS, 1, x, y, pos.x, pos.y );
         return FALSE;
     }
 
@@ -1444,6 +1576,7 @@
                       GrabModeAsync, GrabModeAsync, None, None, CurrentTime ) != GrabSuccess)
     {
         WARN( "refusing to warp pointer to %u, %u without exclusive grab\n", pos.x, pos.y );
+        mouse_diag( MDIAG_SET_POS, 2, x, y, pos.x, pos.y );
         return FALSE;
     }
 
@@ -1456,6 +1589,7 @@
     XNoOp( data->display );
     XFlush( data->display ); /* avoids bad mouse lag in games that do their own mouse warping */
     TRACE( "warped to %d,%d serial %lu\n", x, y, data->warp_serial );
+    mouse_diag( MDIAG_SET_POS, clipping_cursor << 4, x, y, pos.x, pos.y );
     return TRUE;
 }
 
@@ -1473,8 +1607,19 @@
     ret = XQueryPointer( display, root_window, &root, &child, &rootX, &rootY, &winX, &winY, &xstate );
     if (ret)
     {
+        static POINT last_x, last_win;
+        static unsigned int last_state;
         POINT old = *pos;
         *pos = root_to_virtual_screen( winX, winY );
+        if (pos->x != last_x.x || pos->y != last_x.y || old.x != last_win.x || old.y != last_win.y ||
+            (xstate & 0x1f00) != last_state)
+        {
+            /* a,b: X pointer; c,d: position win32u had before asking */
+            mouse_diag( MDIAG_GET_POS, (xstate >> 8) & 0x1f, pos->x, pos->y, old.x, old.y );
+            last_x = *pos;
+            last_win = old;
+            last_state = xstate & 0x1f00;
+        }
         TRACE( "pointer at %s server pos %s\n", wine_dbgstr_point(pos), wine_dbgstr_point(&old) );
     }
     return ret;
@@ -1586,6 +1731,7 @@
     }
 
     pt = map_event_coords( hwnd, event->window, event->root, root, pt );
+    mouse_diag( MDIAG_BUTTON, 0x100 | button, pt.x, pt.y, event->x_root, event->y_root );
     send_mouse_input( hwnd, pt, MOUSEEVENTF_ABSOLUTE | flags, button_down_data[button], time, NULL );
     return TRUE;
 }
@@ -1605,6 +1751,7 @@
     TRACE( "hwnd %p/%lx button %u pos %s\n", hwnd, event->window, button, wine_dbgstr_point( &pt ) );
 
     pt = map_event_coords( hwnd, event->window, event->root, root, pt );
+    mouse_diag( MDIAG_BUTTON, button, pt.x, pt.y, event->x_root, event->y_root );
     send_mouse_input( hwnd, pt, MOUSEEVENTF_ABSOLUTE | flags, button_up_data[button], time, NULL );
     return TRUE;
 }
@@ -1625,10 +1772,12 @@
     if (is_old_motion_event( event->serial ))
     {
         TRACE( "pos %d,%d old serial %lu, ignoring\n", event->x, event->y, event->serial );
+        mouse_diag( MDIAG_MOTION, 1 | (clipping_cursor << 1), event->x, event->y, event->x_root, event->y_root );
         return FALSE;
     }
 
     pt = map_event_coords( hwnd, event->window, event->root, root, pt );
+    mouse_diag( MDIAG_MOTION, clipping_cursor << 1, pt.x, pt.y, event->x_root, event->y_root );
     send_mouse_input( hwnd, pt, MOUSEEVENTF_ABSOLUTE, 0, time, NULL );
     return TRUE;
 }
@@ -1652,10 +1801,12 @@
     if (is_old_motion_event( event->serial ))
     {
         TRACE( "pos %d,%d old serial %lu, ignoring\n", event->x, event->y, event->serial );
+        mouse_diag( MDIAG_ENTER, 1 | (clipping_cursor << 1), event->x, event->y, event->x_root, event->y_root );
         return FALSE;
     }
 
     pt = map_event_coords( hwnd, event->window, event->root, root, pt );
+    mouse_diag( MDIAG_ENTER, clipping_cursor << 1, pt.x, pt.y, event->x_root, event->y_root );
     send_mouse_input( hwnd, pt, MOUSEEVENTF_ABSOLUTE, 0, time, NULL );
     return TRUE;
 }
@@ -1672,6 +1823,7 @@
 
     if (event->deviceid != data->xinput2_pointer) return FALSE;
     if (event->reason != XISlaveSwitch) return FALSE;
+    mouse_diag( MDIAG_DEVICE_SWITCH, 0, event->sourceid, 0, 0, 0 );
 
     update_relative_valuators( event->classes, event->num_classes );
     update_device_mapping( event->display, event->sourceid );
@@ -1758,10 +1910,14 @@
     if (broken_rawevents && is_old_motion_event( xev->serial ))
     {
         TRACE( "old serial %lu, ignoring\n", xev->serial );
+        mouse_diag( MDIAG_RAW_MOTION, 1, 0, 0, 0, 0 );
         return FALSE;
     }
 
     pt = map_raw_event_coords( event, raw.data );
+    /* flags: 2 = clipping (pt is the relative move sent to win32u) */
+    mouse_diag( MDIAG_RAW_MOTION, x11drv_thread_data()->clipping_cursor << 1,
+                raw.data[0].x, raw.data[0].y, pt.x, pt.y );
     send_mouse_input( NULL, pt, 0, 0, time, &raw );
     return TRUE;
 }
@@ -1800,6 +1956,8 @@
     }
 
     pt = map_raw_event_coords( event, raw.data );
+    mouse_diag( MDIAG_RAW_BUTTON, (event->evtype == XI_RawButtonRelease ? 0 : 0x100) | button,
+                raw.data[0].x, raw.data[0].y, pt.x, pt.y );
     send_mouse_input( NULL, pt, flags, data, time, &raw );
     return TRUE;
 }
@@ -1810,7 +1968,7 @@
     INPUT input = {.type = INPUT_HARDWARE};
     XIDeviceEvent *event = xev->data;
     POINT pt = { event->event_x, event->event_y }, root = { event->root_x, event->root_y };
-    int flags = 0;
+    int flags = 0, i;
     POINT pos;
 
     pt = map_event_coords( hwnd, event->event, event->root, root, pt );
@@ -1836,6 +1994,21 @@
 
     input.hi.wParamL = event->detail;
     input.hi.wParamH = POINTER_MESSAGE_FLAG_INRANGE | POINTER_MESSAGE_FLAG_INCONTACT | flags;
+
+    for (i = 0; i < active_touch_count; i++) if (active_touches[i].id == event->detail) break;
+    if (event->evtype == XI_TouchEnd)
+    {
+        if (i < active_touch_count) active_touches[i] = active_touches[--active_touch_count];
+    }
+    else if (i < active_touch_count || active_touch_count < MAX_ACTIVE_TOUCHES)
+    {
+        if (i == active_touch_count) active_touch_count++;
+        active_touches[i].id = event->detail;
+        active_touches[i].hwnd = hwnd;
+        active_touches[i].pos = MAKELPARAM( pos.x, pos.y );
+    }
+    mouse_diag( MDIAG_TOUCH, event->evtype - XI_TouchBegin, event->detail, pt.x, pt.y, event->sourceid );
+
     NtUserSendHardwareInput( hwnd, 0, &input, MAKELPARAM( pos.x, pos.y ) );
 
     return TRUE;
