--- dlls/winex11.drv/event.c.orig
+++ dlls/winex11.drv/event.c
@@ -693,6 +693,8 @@
 
     TRACE( "window %p/%lx FocusIn serial %lu, detail %s, mode %s, foreground %p\n", hwnd, event->window,
            event->serial, focus_details[event->detail], focus_modes[event->mode], foreground );
+    mouse_diag( MDIAG_FOCUS_IN, event->mode | (event->detail << 4) | (keyboard_grabbed << 8),
+                HandleToULong( hwnd ), HandleToULong( foreground ), event->window, 0 );
 
     if (event->detail == NotifyPointer) return FALSE;
     if (event->mode != NotifyWhileGrabbed && event->detail != NotifyVirtual && event->detail != NotifyNonlinearVirtual)
@@ -720,6 +722,9 @@
     /* when keyboard grab is released, re-apply the cursor clipping rect */
     was_grabbed = keyboard_grabbed;
     keyboard_grabbed = event->mode == NotifyGrab || event->mode == NotifyWhileGrabbed;
+    if (was_grabbed != keyboard_grabbed)
+        mouse_diag( MDIAG_FOCUS_IN, event->mode | (event->detail << 4) | (keyboard_grabbed << 8),
+                    HandleToULong( hwnd ), HandleToULong( foreground ), event->window, 1 );
     if (was_grabbed > keyboard_grabbed) reapply_cursor_clipping();
     /* ignore wm specific NotifyUngrab / NotifyGrab events w.r.t focus */
     if (event->mode == NotifyGrab || event->mode == NotifyUngrab) return FALSE;
@@ -784,6 +789,8 @@
 
     TRACE( "window %p/%lx FocusOut serial %lu, detail %s, mode %s, foreground %p\n", hwnd, event->window,
            event->serial, focus_details[event->detail], focus_modes[event->mode], foreground );
+    mouse_diag( MDIAG_FOCUS_OUT, event->mode | (event->detail << 4) | (keyboard_grabbed << 8),
+                HandleToULong( hwnd ), HandleToULong( foreground ), event->window, 0 );
 
     if (event->detail == NotifyPointer)
     {
@@ -820,7 +827,10 @@
 
     /* in virtual desktop mode or when keyboard is grabbed, release any cursor grab but keep the clipping rect */
     keyboard_grabbed = event->mode == NotifyGrab || event->mode == NotifyWhileGrabbed;
+    mouse_diag( MDIAG_FOCUS_OUT, event->mode | (event->detail << 4) | (keyboard_grabbed << 8),
+                HandleToULong( hwnd ), HandleToULong( foreground ), event->window, 1 );
     if (is_virtual_desktop() || keyboard_grabbed) ungrab_clipping_window();
+    end_active_touches();
     /* ignore wm specific NotifyUngrab / NotifyGrab events w.r.t focus */
     if (event->mode == NotifyGrab || event->mode == NotifyUngrab) return FALSE;
 
