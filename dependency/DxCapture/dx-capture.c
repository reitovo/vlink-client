//
// Created by reito on 2023/1/28.
//

#include "dx-capture.h"
#include "graphics-hook-info.h"
#include "get-graphics-offsets/get-graphics-offsets.h"
#include <windows.h>
#include <d3d11.h>

extern struct graphics_offsets offsets;

bool load_graphics_offsets() {

    WNDCLASSA wc = {0};
    wc.style = CS_OWNDC;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpfnWndProc = (WNDPROC) DefWindowProcA;
    wc.lpszClassName = DUMMY_WNDCLASS;

    SetErrorMode(SEM_FAILCRITICALERRORS);

    if (!RegisterClassA(&wc)) {
        printf("failed to register '%s'\n", DUMMY_WNDCLASS);
        return -1;
    }

    get_d3d8_offsets(&offsets.d3d8);
    get_d3d9_offsets(&offsets.d3d9);
    get_dxgi_offsets(&offsets.dxgi, &offsets.dxgi2);

    return true;
}

static DWORD WINAPI init_hooks(LPVOID param) {
    load_graphics_offsets();
    return 0;
}

static HANDLE init_hooks_thread = NULL;

void wait_for_hook_initialization(void) {
    static bool initialized = false;

    if (!initialized) {
        if (init_hooks_thread) {
            WaitForSingleObject(init_hooks_thread, INFINITE);
            CloseHandle(init_hooks_thread);
            init_hooks_thread = NULL;
        }
        initialized = true;
    }
}

void dx_capture_load() {
    init_hooks_thread =
            CreateThread(NULL, 0, init_hooks, NULL, 0, NULL);
}

void dx_capture_unload() {
    wait_for_hook_initialization();
}

void *game_capture_create(struct dx_capture_t* cap);
void game_capture_destroy(void *data);
uint32_t game_capture_width(void *data);
uint32_t game_capture_height(void *data);
void game_capture_update(void *data, struct dx_capture_setting_t *settings);
void game_capture_tick(void *data, float seconds);

void dx_capture_init(struct dx_capture_t *ctx) {
    if (ctx == NULL)
        return;

    ctx->gc = game_capture_create(ctx);
}

void dx_capture_destroy(struct dx_capture_t *ctx) {
    if (ctx == NULL)
        return;

    game_capture_destroy(ctx->gc);
    ctx->gc = NULL;
}

uint32_t dx_capture_width(struct dx_capture_t *ctx) {
    if (ctx == NULL)
        return 0;

    return game_capture_width(ctx->gc);
}

uint32_t dx_capture_height(struct dx_capture_t *ctx) {
    if (ctx == NULL)
        return 0;

    return game_capture_height(ctx->gc);
}

void dx_capture_update_settings(struct dx_capture_t *ctx) {
    if (ctx == NULL)
        return;

    game_capture_update(ctx->gc, &ctx->setting);
}

void dx_capture_tick(struct dx_capture_t *ctx, float seconds) {
    if (ctx == NULL)
        return;

    game_capture_tick(ctx->gc, seconds);
}

void dx_capture_setting_default(struct dx_capture_setting_t *setting) {
    setting->limit_framerate = false;
    setting->capture_overlays = false;
    setting->anticheat_hook = false;
    setting->target_fps.den = 60;
    setting->target_fps.num = 1;
}

char *dx_module_file(struct dx_capture_t *ctx, const char *filename) {
    if (ctx->get_file_path)
        return ctx->get_file_path(filename);
    else
        return filename;
}

void dx_graphic_lock(struct dx_capture_t *ctx) {
    ctx->lock();
}

void dx_graphic_unlock(struct dx_capture_t *ctx) {
    ctx->unlock();
}