#ifndef WAYLAND_BASIC_H
#define WAYLAND_BASIC_H

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <wayland-client-protocol.h>
#include <stdint.h>

// Структура для хранения состояния Wayland-клиента
typedef struct WaylandContext {
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_compositor* compositor;
    struct wl_surface* surface;
    struct xdg_wm_base* xdg_wm_base;
    struct xdg_surface* xdg_surface;
    struct xdg_toplevel* xdg_toplevel;
    struct wl_shm* shm;
    struct wl_buffer* buffer;
    int width;
    int height;
    void* shm_data;
    int shm_fd;
    size_t shm_size;
    int running;
} WaylandContext;

// Инициализация Wayland-клиента
int wayland_init(WaylandContext* ctx);
// Завершение работы и очистка ресурсов
void wayland_cleanup(WaylandContext* ctx);
int wayland_show_window(WaylandContext* ctx, int width, int height, uint32_t color);

#endif // WAYLAND_BASIC_H 