#include "wayland_basic.h"
#include <stdio.h>
#include <stdlib.h>

// Callback для получения глобальных объектов
static void registry_handler(void* data, struct wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
    WaylandContext* ctx = (WaylandContext*)data;
    if (strcmp(interface, "wl_compositor") == 0) {
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    }
}

static void registry_remover(void* data, struct wl_registry* registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = registry_remover
};

int wayland_init(WaylandContext* ctx) {
    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) {
        fprintf(stderr, "[Wayland] Не удалось подключиться к Wayland серверу!\n");
        return 1;
    }
    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);
    if (!ctx->compositor) {
        fprintf(stderr, "[Wayland] Не удалось получить wl_compositor!\n");
        wl_display_disconnect(ctx->display);
        return 1;
    }
    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    if (!ctx->surface) {
        fprintf(stderr, "[Wayland] Не удалось создать wl_surface!\n");
        wl_display_disconnect(ctx->display);
        return 1;
    }
    printf("[Wayland] Инициализация успешна!\n");
    return 0;
}

void wayland_cleanup(WaylandContext* ctx) {
    if (ctx->surface) wl_surface_destroy(ctx->surface);
    if (ctx->compositor) wl_compositor_destroy(ctx->compositor);
    if (ctx->registry) wl_registry_destroy(ctx->registry);
    if (ctx->display) wl_display_disconnect(ctx->display);
    printf("[Wayland] Очистка завершена.\n");
} 