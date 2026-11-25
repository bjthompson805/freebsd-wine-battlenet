--- dlls/winewayland.drv/window.c.orig
+++ dlls/winewayland.drv/window.c
@@ -412,6 +412,28 @@ void WAYLAND_DestroyWindow(HWND hwnd)
     wayland_win_data_destroy(data);
 }
 
+/***********************************************************************
+ *           WAYLAND_GetWindowStyleMasks
+ *
+ * The styles the compositor stands in for, so that win32u leaves their
+ * non-client area out of the window, as winex11 does for windows its
+ * window manager decorates: top-level windows with a full caption lose the
+ * Windows-style title bar and frame.
+ */
+BOOL WAYLAND_GetWindowStyleMasks(HWND hwnd, UINT style, UINT ex_style, UINT *style_mask, UINT *ex_style_mask)
+{
+    *style_mask = *ex_style_mask = 0;
+
+    if (ex_style & (WS_EX_TOOLWINDOW | WS_EX_LAYERED)) return TRUE;
+    if ((style & WS_CAPTION) != WS_CAPTION) return TRUE;
+    if (NtUserGetAncestor(hwnd, GA_PARENT) != NtUserGetDesktopWindow()) return TRUE;
+
+    *style_mask = WS_CAPTION | WS_DLGFRAME | WS_THICKFRAME;
+    *ex_style_mask = WS_EX_DLGMODALFRAME;
+    return TRUE;
+}
+
+
 /***********************************************************************
  *           WAYLAND_WindowPosChanging
  */
