#define _GNU_SOURCE
#include "wayland_basic.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <wayland-client-protocol.h>

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
    (void)data; (void)xdg_wm_base; (void)serial;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void shm_format(void* data, struct wl_shm* shm, uint32_t format) {
    (void)data; (void)shm; (void)format;
    // Можно обработать поддерживаемые форматы, если нужно
}

static const struct wl_shm_listener shm_listener = {
    .format = shm_format,
};

// Callback для получения глобальных объектов
static void registry_handler(void* data, struct wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
    (void)data; (void)registry; (void)id; (void)interface; (void)version;
    WaylandContext* ctx = (WaylandContext*)data;
    if (strcmp(interface, "wl_compositor") == 0) {
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        ctx->xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(ctx->xdg_wm_base, &xdg_wm_base_listener, ctx);
    } else if (strcmp(interface, "wl_shm") == 0) {
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
        wl_shm_add_listener(ctx->shm, &shm_listener, ctx);
    }
}

static void registry_remover(void* data, struct wl_registry* registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = registry_remover
};

static void toplevel_close(void* data, struct xdg_toplevel* toplevel) {
    (void)data; (void)toplevel;
    WaylandContext* ctx = (WaylandContext*)data;
    ctx->running = 0;
}

static void toplevel_configure(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states) {
    (void)toplevel; (void)states;
    WaylandContext* ctx = (WaylandContext*)data;
    if (width > 0) ctx->width = width;
    if (height > 0) ctx->height = height;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

// --- ДОБАВИТЬ вспомогательную структуру для передачи состояния configure ---
typedef struct {
    WaylandContext* ctx;
    int* configured_ptr;
} ConfigureContext;

static void xdg_surface_configure(void* data, struct xdg_surface* surface, uint32_t serial) {
    ConfigureContext* cctx = (ConfigureContext*)data;
    xdg_surface_ack_configure(surface, serial);
    if (cctx && cctx->configured_ptr) *(cctx->configured_ptr) = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void* create_shm_buffer(WaylandContext* ctx, int width, int height, uint32_t color) {
    ctx->width = width;
    ctx->height = height;
    ctx->shm_size = width * height * 4;
    ctx->shm_fd = -1;
    ctx->shm_data = NULL;
    char shm_name[] = "/wayland-shm-XXXXXX";
    ctx->shm_fd = mkstemp(shm_name);
    if (ctx->shm_fd < 0) return NULL;
    unlink(shm_name);
    if (ftruncate(ctx->shm_fd, ctx->shm_size) < 0) {
        close(ctx->shm_fd);
        return NULL;
    }
    ctx->shm_data = mmap(NULL, ctx->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->shm_fd, 0);
    if (ctx->shm_data == MAP_FAILED) {
        close(ctx->shm_fd);
        return NULL;
    }
    // Заливка цветом
    uint32_t* pixels = (uint32_t*)ctx->shm_data;
    for (int i = 0; i < width * height; ++i) pixels[i] = color;
    struct wl_shm_pool* pool = wl_shm_create_pool(ctx->shm, ctx->shm_fd, ctx->shm_size);
    ctx->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    return ctx->shm_data;
}

int wayland_init(WaylandContext* ctx) {
    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) {
        fprintf(stderr, "[Wayland] Не удалось подключиться к Wayland серверу!\n");
        return 1;
    }
    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);
    if (!ctx->compositor || !ctx->xdg_wm_base || !ctx->shm) {
        fprintf(stderr, "[Wayland] Не удалось получить все необходимые объекты (compositor/xdg_wm_base/shm)!\n");
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
    if (ctx->buffer) {
        wl_buffer_destroy(ctx->buffer);
        ctx->buffer = NULL;
    }
    if (ctx->xdg_toplevel) {
        xdg_toplevel_destroy(ctx->xdg_toplevel);
        ctx->xdg_toplevel = NULL;
    }
    if (ctx->xdg_surface) {
        xdg_surface_destroy(ctx->xdg_surface);
        ctx->xdg_surface = NULL;
    }
    if (ctx->surface) {
        wl_surface_destroy(ctx->surface);
        ctx->surface = NULL;
    }
    if (ctx->compositor) {
        wl_compositor_destroy(ctx->compositor);
        ctx->compositor = NULL;
    }
    if (ctx->shm_data && ctx->shm_size > 0) {
        munmap(ctx->shm_data, ctx->shm_size);
        ctx->shm_data = NULL;
        ctx->shm_size = 0;
    }
    if (ctx->shm_fd >= 0) {
        close(ctx->shm_fd);
        ctx->shm_fd = -1;
    }
    if (ctx->shm) {
        wl_shm_destroy(ctx->shm);
        ctx->shm = NULL;
    }
    if (ctx->registry) {
        wl_registry_destroy(ctx->registry);
        ctx->registry = NULL;
    }
    if (ctx->display) {
        wl_display_disconnect(ctx->display);
        ctx->display = NULL;
    }
    printf("[Wayland] Очистка завершена.\n");
}

int wayland_show_window(WaylandContext* ctx, int width, int height, uint32_t color) {
    if (wayland_init(ctx) != 0) return 1;
    ctx->xdg_surface = xdg_wm_base_get_xdg_surface(ctx->xdg_wm_base, ctx->surface);
    if (!ctx->xdg_surface) {
        fprintf(stderr, "[Wayland] Не удалось создать xdg_surface!\n");
        wayland_cleanup(ctx);
        return 1;
    }
    int configured = 0;
    ConfigureContext cctx = { .ctx = ctx, .configured_ptr = &configured };
    xdg_surface_add_listener(ctx->xdg_surface, &xdg_surface_listener, &cctx);
    ctx->xdg_toplevel = xdg_surface_get_toplevel(ctx->xdg_surface);
    if (!ctx->xdg_toplevel) {
        fprintf(stderr, "[Wayland] Не удалось создать xdg_toplevel!\n");
        wayland_cleanup(ctx);
        return 1;
    }
    xdg_toplevel_add_listener(ctx->xdg_toplevel, &toplevel_listener, ctx);
    wl_surface_commit(ctx->surface);
    // Ждём configure
    while (!configured) {
        wl_display_dispatch(ctx->display);
    }
    if (!create_shm_buffer(ctx, width, height, color)) {
        fprintf(stderr, "[Wayland] Не удалось создать shm-буфер!\n");
        wayland_cleanup(ctx);
        return 1;
    }
    wl_surface_attach(ctx->surface, ctx->buffer, 0, 0);
    wl_surface_damage_buffer(ctx->surface, 0, 0, width, height);
    wl_surface_commit(ctx->surface);
    ctx->running = 1;
    while (ctx->running) {
        wl_display_dispatch(ctx->display);
    }
    wayland_cleanup(ctx);
    return 0;
} 