#ifndef NDI_TO_D3D_H
#define NDI_TO_D3D_H

#include "core/debugcenter.h"
#include "core/util.h"
#include "i_d3d_src.h"
#include "qmutex.h"
#include <wrl/client.h>
#include <QString>
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;
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
struct NDIlib_video_frame_v2_t;

// This is used by server side to directly convert ndi frames to d3d11texture2d.
// Then it can be directly used by DxToNdi merger, without need to encode/decode
class FrameToDx : public IDxSrc, public IDebugCollectable
{
    ComPtr<ID3D11DeviceContext> _d3d11_deviceCtx = nullptr;
    ComPtr<ID3D11Device> _d3d11_device = nullptr;

    ComPtr<ID3D11Texture2D> _texture_rgba = nullptr;

    HANDLE _texture_rgba_shared = nullptr;

    QMutex lock;

    std::atomic_bool _inited;

    uint32_t _width{ 0 };
    uint32_t _height{ 0 };

    FpsCounter fps;

public:
    FrameToDx(std::shared_ptr<DxToFrame>);
    ~FrameToDx();

    QString debugInfo();

    bool init();
    bool createSharedSurf(int width, int height);
    void releaseSharedSurf();

    void update(NDIlib_video_frame_v2_t*);
    bool copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D *dest);
};

#endif // NDI_TO_D3D_H
