//
// Created by reito on 2023/1/28.
//
#pragma once

#if __cplusplus
extern "C" {
#endif

#include "util/c99defs.h"

typedef struct ID3D11Texture2D dx_texture_t;

struct dx_frac {
    int num;
    int den;
};

struct dx_capture_setting_t {
    bool limit_framerate;
    bool capture_overlays;
    bool anticheat_hook;
    bool force_shmem;
    const char *window;
    struct dx_frac target_fps;
};

struct dx_capture_t {
    struct ID3D11Device *device;
    struct ID3D11DeviceContext *context;
    struct dx_capture_setting_t setting;
    void *gc;
    void *user;

    void (*lock)(void* user);
    void (*unlock)(void* user);

    char* (*on_get_hook_file_path)(const char * filename);
    void (*on_captured_texture)(void* user, dx_texture_t* texture);
};

void dx_capture_setting_default(struct dx_capture_setting_t* setting);

void dx_capture_load();
void dx_capture_unload();

void dx_capture_init(struct dx_capture_t *ctx);
void dx_capture_destroy(struct dx_capture_t *ctx);
uint32_t dx_capture_width(struct dx_capture_t *ctx);
uint32_t dx_capture_height(struct dx_capture_t *ctx);
void dx_capture_update_settings(struct dx_capture_t *ctx);
void dx_capture_tick(struct dx_capture_t *ctx, float seconds);

char *dx_module_file(struct dx_capture_t *ctx, const char *filename);

#define DX_TEXTURE_FLAG_DYNAMIC (1 << 1)
#define DX_TEXTURE_FLAG_RENDER_TARGET (1 << 2)
#define DX_TEXTURE_FLAG_SHARED_TEX (1 << 3)

void dx_graphic_lock(struct dx_capture_t *ctx);
void dx_graphic_unlock(struct dx_capture_t *ctx);
dx_texture_t *dx_texture_create(struct dx_capture_t *ctx, uint32_t width, uint32_t height, uint32_t dxgi_format, uint32_t levels, const uint8_t **data, uint32_t flags);
dx_texture_t *dx_texture_open_shared(struct dx_capture_t *ctx, uint32_t handle);
bool dx_texture_map(struct dx_capture_t *ctx, dx_texture_t *tex, uint8_t **ptr, uint32_t *linesize);
void dx_texture_unmap(struct dx_capture_t *ctx, dx_texture_t *tex);
void dx_texture_destroy(struct dx_capture_t *ctx, dx_texture_t *tex);

#if __cplusplus
};
#endif