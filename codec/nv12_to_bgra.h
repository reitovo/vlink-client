#ifndef NV12_TO_BGRA_H
#define NV12_TO_BGRA_H

extern "C" {
#include "libavutil/frame.h"
}

#include "qmutex.h"
#include <memory>
#include <wrl/client.h>
#include <QString>
#include <d3dcompiler.h>
#include <queue>
#include "core/vtslink.h"

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
using Microsoft::WRL::ComPtr;

namespace {
    struct DxFrame {
        int64_t pts;
        ComPtr<ID3D11Texture2D> frame;
    };

    class DxFrameBuffer {
    public:
        std::queue<DxFrame> free;
        std::queue<DxFrame> queue;

        int size() const;
        ComPtr<ID3D11Texture2D> getFree();
        void addFree(ComPtr<ID3D11Texture2D> f);

        int64_t topPts();
        ComPtr<ID3D11Texture2D> dequeue();
        void enqueue(ComPtr<ID3D11Texture2D> f, int64_t pts);
        void clear();
    };
}

// Thanks to
// https://github.com/microsoft/Windows-universal-samples/blob/main/Samples/HolographicFaceTracking/cpp/Content/NV12VideoTexture.cpp
// https://gist.github.com/tqk2811/9dc56339499fb0355e1e114a49596214
//
// This class is dedicated to be used by AvToDx, combining two separate streams to one BGRA texture
// Because we use two streams to send RGB and Alpha in NV12 format, we need a fast path to convert them
// back, luckily, ffmpeg can decode use d3d11 and we can directly use textures as shader input to do a
// fast shader calculation, getting a BGRA texture.
class Nv12ToBgra {
    ComPtr<ID3D11DeviceContext> _d3d11_deviceCtx = nullptr;
    ComPtr<ID3D11Device> _d3d11_device = nullptr;
    ComPtr<ID3D11SamplerState> _d3d11_samplerState = nullptr;
    ComPtr<ID3D11VertexShader> _d3d11_vertexShader = nullptr;
    ComPtr<ID3D11InputLayout> _d3d11_inputLayout = nullptr;
    ComPtr<ID3D11PixelShader> _d3d11_pixelShader = nullptr;
    ComPtr<ID3D11Buffer> _d3d11_vertexBuffer = nullptr;

    ComPtr<ID3D11Texture2D> _texture_nv12 = nullptr;
    ComPtr<ID3D11ShaderResourceView> _luminanceView = nullptr;
    ComPtr<ID3D11ShaderResourceView> _chrominanceView = nullptr;
    ComPtr<ID3D11RenderTargetView> _renderTargetView = nullptr;
    ComPtr<ID3D11Texture2D> _texture_rgba_target = nullptr;
    ComPtr<ID3D11Texture2D> _texture_rgba_copy = nullptr;

    ComPtr<ID3DBlob> _vertex_shader = nullptr;
    ComPtr<ID3DBlob> _pixel_shader = nullptr;

    HANDLE _texture_rgba_copy_shared = nullptr;

    uint32_t _width;
    uint32_t _height;

    std::atomic_bool _inited = false;

public:
    Nv12ToBgra(int width, int height);
    ~Nv12ToBgra();

    QMutex lock;

    bool init();
    bool compileShader();
    void resetDeviceContext();
    bool createSharedSurf();
    void releaseSharedSurf();
    ID3D11Device* getDevice();

    bool nv12ToBgra(AVFrame* f);
    bool copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* dest);
    ID3D11Texture2D* getSharedTargetTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx);
};

#endif // NV12_TO_BGRA_H
