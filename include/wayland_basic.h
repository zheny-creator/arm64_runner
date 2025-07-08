#ifndef WAYLAND_BASIC_H
#define WAYLAND_BASIC_H

#include <wayland-client.h>

// Структура для хранения состояния Wayland-клиента
typedef struct {
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_compositor* compositor;
    struct wl_surface* surface;
} WaylandContext;

// Инициализация Wayland-клиента
int wayland_init(WaylandContext* ctx);
// Завершение работы и очистка ресурсов
void wayland_cleanup(WaylandContext* ctx);

#endif // WAYLAND_BASIC_H 