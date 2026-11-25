--- dlls/winewayland.drv/waylanddrv.h.orig
+++ dlls/winewayland.drv/waylanddrv.h
@@ -475,6 +475,7 @@ UINT WAYLAND_UpdateDisplayDevices(const struct gdi_device_manager *device_manage
 LRESULT WAYLAND_WindowMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
 void WAYLAND_WindowPosChanged(HWND hwnd, HWND insert_after, HWND owner_hint, UINT swp_flags,
                               const struct window_rects *new_rects, struct window_surface *surface);
+BOOL WAYLAND_GetWindowStyleMasks(HWND hwnd, UINT style, UINT ex_style, UINT *style_mask, UINT *ex_style_mask);
 BOOL WAYLAND_WindowPosChanging(HWND hwnd, UINT swp_flags, BOOL shaped, const struct window_rects *rects);
 struct client_surface *WAYLAND_CreateClientSurface(HWND hwnd, int pixel_format, BOOL raw);
 BOOL WAYLAND_CreateWindowSurface(HWND hwnd, BOOL layered, const RECT *surface_rect, struct window_surface **surface);
