--- dlls/winex11.drv/x11drv.h.orig
+++ dlls/winex11.drv/x11drv.h
@@ -770,6 +770,16 @@
 extern void X11DRV_ActivateWindow( HWND hwnd, HWND previous );
 extern void reapply_cursor_clipping(void);
 extern void ungrab_clipping_window(void);
+extern void end_active_touches(void);
+
+/* mouse diagnostics ring (WINE_MOUSE_DIAG=<dir>), see mouse.c */
+enum mouse_diag_type
+{
+    MDIAG_MOTION = 1, MDIAG_ENTER, MDIAG_BUTTON, MDIAG_RAW_MOTION, MDIAG_RAW_BUTTON,
+    MDIAG_SET_POS, MDIAG_GET_POS, MDIAG_CLIP, MDIAG_UNCLIP, MDIAG_FOCUS_IN, MDIAG_FOCUS_OUT,
+    MDIAG_DEVICE_SWITCH, MDIAG_TOUCH,
+};
+extern void mouse_diag( unsigned short type, unsigned short flags, int a, int b, int c, int d );
 extern void move_resize_window( HWND hwnd, int dir, POINT pos );
 extern void x11drv_init_keyboard( Display *display );
 extern void x11drv_init_mouse( Display *display );
