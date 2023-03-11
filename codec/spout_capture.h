//
// Created by reito on 2023/1/28.
//

#ifndef VTSLINK_SPOUT_CAPTURE_H
#define VTSLINK_SPOUT_CAPTURE_H

#include "core/debug_center.h"
#include "core/util.h"
#include "i_d3d_src.h"
#include "qmutex.h"
#include <wrl/client.h>
#include <QString>
#include <d3dcompiler.h>
#include <directxcolors.h>
#include "d3d11.h"

#include "SpoutGL/SpoutDirectX.h"
#include "SpoutGL/SpoutSenderNames.h"
#include "SpoutGL/SpoutFrameCount.h"
#include "SpoutGL/SpoutUtils.h"

using Microsoft::WRL::ComPtr;

class SpoutCapture : public IDxToFrameSrc, public IDebugCollectable {
    ComPtr<ID3D11DeviceContext> _d3d11_deviceCtx = nullptr;
    ComPtr<ID3D11Device> _d3d11_device = nullptr;

    ComPtr<ID3D11SamplerState> _d3d11_samplerState = nullptr;
    ComPtr<ID3D11InputLayout> _d3d11_inputLayout = nullptr;
    ComPtr<ID3D11VertexShader> _d3d11_scale_vs = nullptr;
    ComPtr<ID3D11PixelShader> _d3d11_scale_ps = nullptr;
    ComPtr<ID3D11Buffer> _d3d11_vertexBuffer = nullptr;

    ComPtr<ID3D11RenderTargetView> _rtv_target_bgra = nullptr;

    ComPtr<ID3DBlob> _scale_vertex_shader = nullptr;
    ComPtr<ID3DBlob> _scale_pixel_shader = nullptr;

    ComPtr<ID3D11ShaderResourceView> _captured_view = nullptr;

    // Receiver Texture
    ComPtr<ID3D11Texture2D> _texture_bgra = nullptr;
    HANDLE _texture_bgra_shared = nullptr;

    // Sender Texture
    ID3D11Texture2D * _texture_captured = nullptr;
    HANDLE _texture_captured_shared = nullptr;

    QMutex lock;

    std::atomic_bool _inited = false;

    uint32_t _width{0};
    uint32_t _height{0};

    uint32_t _capturedWidth{0};
    uint32_t _capturedHeigth{0};

    FpsCounter fps;

    std::string spoutName;
    spoutSenderNames spoutSender;
    spoutDirectX spoutDx;
    spoutFrameCount spoutFrame;

    std::atomic_bool _spoutInited = false;

    bool createSharedSurf(int width, int height);
    void releaseSharedSurf();

    bool compileShader();
    void resetDeviceContext();

public:
    explicit SpoutCapture(const std::shared_ptr<DxToFrame>& d3d, std::string spoutName);
    ~SpoutCapture();

    QString debugInfo() override;

    bool init();

    void captureTick(float time);

    bool copyTo(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *dest) override;
};

#endif //VTSLINK_SPOUT_CAPTURE_H
