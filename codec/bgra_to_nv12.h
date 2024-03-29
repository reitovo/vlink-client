#ifndef BGRANV12_H
#define BGRANV12_H

extern "C" {
#include "libavutil/frame.h"
}

#include "core/vtslink.h"
#include "qmutex.h"
#include <memory>
#include <wrl/client.h>
#include <QString>
#include <d3dcompiler.h>
#include <array>
#include "core/util.h"
#include "i_d3d_src.h"

class DxToFrame;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11SamplerState;
struct ID3D11VertexShader;
struct ID3D11InputLayout;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
struct D3D11_MAPPED_SUBRESOURCE;
using Microsoft::WRL::ComPtr;

// Thanks to
// https://github.com/wangf1978/DirectXVideoScreen
// https://stackoverflow.com/questions/12730309/rendering-to-multiple-textures-with-one-pass-in-directx-11
// https://github.com/mofo7777/DirectXVideoScreen/tree/master/DirectXVideoScreen/D3D11ShaderNV12
// https://blog.csdn.net/weixin_38884324/article/details/80570160
//
// This class is used to convert bgra to two nv12 textures using shader. Used by ndi to av only.
// Specifically, if we are using nvenc, we can even directly pass the d3d11texture as input,
// omitting the copy process.
class BgraToNv12
{
    ComPtr<ID3D11DeviceContext> _d3d11_deviceCtx = nullptr;
    ComPtr<ID3D11Device> _d3d11_device = nullptr;
    ComPtr<ID3D11SamplerState> _d3d11_samplerState = nullptr;
    ComPtr<ID3D11InputLayout> _d3d11_inputLayout = nullptr;
    ComPtr<ID3D11VertexShader> _d3d11_bgra_nv24_vs = nullptr;
    ComPtr<ID3D11PixelShader> _d3d11_bgra_nv24_ps = nullptr;
    ComPtr<ID3D11VertexShader> _d3d11_nv24_nv12_vs = nullptr;
    ComPtr<ID3D11PixelShader> _d3d11_nv24_nv12_ps = nullptr;
    ComPtr<ID3D11Buffer> _d3d11_vertexBuffer = nullptr;

    ComPtr<ID3D11ShaderResourceView> _bgraView = nullptr;
    ComPtr<ID3D11ShaderResourceView> _downsampleView = nullptr;

    ComPtr<ID3D11RenderTargetView> _rtv_bgra = nullptr;
    ComPtr<ID3D11RenderTargetView> _rtv_bgra_uv = nullptr;
    ComPtr<ID3D11RenderTargetView> _rtv_nv12_y = nullptr;
    ComPtr<ID3D11RenderTargetView> _rtv_nv12_uv = nullptr;

    ComPtr<ID3D11Texture2D> _texture_bgra = nullptr;
    ComPtr<ID3D11Texture2D> _texture_uv_target = nullptr;
    // Resource being set to OM RenderTarget slot 1 is still bound on input!
    ComPtr<ID3D11Texture2D> _texture_uv_target_copy = nullptr;

    // vertically concat
    ComPtr<ID3D11Texture2D> _texture_nv12_target = nullptr;

    // For hw, no need to copy
    ComPtr<ID3D11Texture2D> _texture_nv12_copy_target = nullptr;

    ComPtr<ID3DBlob> _bgra_nv24_vertex_shader = nullptr;
    ComPtr<ID3DBlob> _bgra_nv24_pixel_shader = nullptr;
    ComPtr<ID3DBlob> _nv24_nv12_vertex_shader = nullptr;
    ComPtr<ID3DBlob> _nv24_nv12_pixel_shader = nullptr;

    std::array<ID3D11RenderTargetView*, 2> _bgra_nv24_rt;
    std::array<ID3D11ShaderResourceView*, 1> _bgra_nv24_sr;
    std::array<ID3D11RenderTargetView*, 1> _nv24_nv12_rt;
    std::array<ID3D11ShaderResourceView*, 1> _nv24_nv12_sr;

    uint32_t _width;
    uint32_t _height;

    DeviceAdapterType _vendor;
    std::atomic_bool _inited = false;

    bool _mapped = false;

public:
    BgraToNv12(int width, int height);
    ~BgraToNv12();

    QMutex lock;

    bool init();
    bool compileShader();
    void initViewport();
    void initDeviceContext();

    void setBgraToNv24Context();
    void setNv24ToNv12Context();

    bool createSharedSurf();
    void releaseSharedSurf();
    ID3D11Device* getDevice();

    DeviceAdapterType getDeviceVendor();

    bool bgraToNv12Fast(const std::shared_ptr<IDxCopyable>& fast);

    void copyFrameD3D11(AVFrame* f);
    void copyFrameQSV(AVFrame* f);
};

#endif // BGRANV12_H
