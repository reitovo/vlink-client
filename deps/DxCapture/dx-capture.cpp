//
// Created by reito on 2023/1/28.
//
#include "dx-capture.h"
#include <d3d11.h>

extern "C" {

dx_texture_t *dx_texture_create(struct dx_capture_t *ctx, uint32_t width, uint32_t height, uint32_t dxgi_format, uint32_t levels, const uint8_t **data, uint32_t flags) {
    bool isDynamic = (flags & DX_TEXTURE_FLAG_DYNAMIC) != 0;
    bool isRenderTarget = (flags & DX_TEXTURE_FLAG_RENDER_TARGET) != 0;
    bool isSharedTexture = (flags & DX_TEXTURE_FLAG_SHARED_TEX) != 0;

    D3D11_TEXTURE2D_DESC texDesc_rgba;
    ZeroMemory(&texDesc_rgba, sizeof(texDesc_rgba));
    texDesc_rgba.Format = (DXGI_FORMAT)dxgi_format;
    texDesc_rgba.Width = width;
    texDesc_rgba.Height = height;
    texDesc_rgba.ArraySize = 1;
    texDesc_rgba.MipLevels = 1;
    texDesc_rgba.SampleDesc.Count = 1;
    texDesc_rgba.SampleDesc.Quality = 0;
    texDesc_rgba.MiscFlags = 0;
    texDesc_rgba.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc_rgba.CPUAccessFlags = isDynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    texDesc_rgba.Usage = isDynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;

    if (isRenderTarget) {
        texDesc_rgba.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }

    if (isSharedTexture) {
        texDesc_rgba.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
    }

    ID3D11Texture2D *texture;
    HRESULT hr = ctx->device->CreateTexture2D(&texDesc_rgba, NULL, &texture);
    if (FAILED(hr)) {
        return NULL;
    }

    return texture;
}

dx_texture_t *dx_texture_open_shared(struct dx_capture_t *ctx, uint32_t handle) {
    HRESULT hr;
    ID3D11Texture2D *texture;
    auto hd = (HANDLE)(uintptr_t)handle;
    hr = ctx->device->OpenSharedResource(hd, __uuidof(ID3D11Texture2D), (void **) &texture);

    if (FAILED(hr)) {
        return NULL;
    }

    return texture;
}

bool dx_texture_map(struct dx_capture_t *ctx, dx_texture_t *tex, uint8_t **ptr, uint32_t *linesize) {
    HRESULT hr;

    D3D11_MAPPED_SUBRESOURCE map;
    hr = ctx->context->Map(tex, 0,
                                     D3D11_MAP_WRITE_DISCARD, 0, &map);
    if (FAILED(hr))
        return false;

    *ptr = (uint8_t *)map.pData;
    *linesize = map.RowPitch;
    return true;
}

void dx_texture_unmap(struct dx_capture_t *ctx, dx_texture_t *tex) {
    ctx->context->Unmap(tex, 0);
}

void dx_texture_destroy(struct dx_capture_t *ctx, dx_texture_t *tex) {
    if(tex != NULL)
        tex->Release();
}

};