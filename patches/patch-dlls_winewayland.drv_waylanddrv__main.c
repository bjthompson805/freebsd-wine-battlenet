--- dlls/winewayland.drv/waylanddrv_main.c.orig
+++ dlls/winewayland.drv/waylanddrv_main.c
@@ -51,6 +51,7 @@ static const struct user_driver_funcs waylanddrv_funcs =
     .pUpdateLayeredWindow = WAYLAND_UpdateLayeredWindow,
     .pUpdateDisplayDevices = WAYLAND_UpdateDisplayDevices,
     .pWindowMessage = WAYLAND_WindowMessage,
+    .pGetWindowStyleMasks = WAYLAND_GetWindowStyleMasks,
     .pWindowPosChanged = WAYLAND_WindowPosChanged,
     .pWindowPosChanging = WAYLAND_WindowPosChanging,
     .pCreateClientSurface = WAYLAND_CreateClientSurface,
