//
// Created by reito on 2023/1/28.
//

#ifndef VTSLINK_D3D_CAPTURE_H
#define VTSLINK_D3D_CAPTURE_H

#include "dx-capture.h"
#include "core/debug_center.h"
#include "core/util.h"
#include "i_d3d_src.h"
#include "qmutex.h"
#include <wrl/client.h>
#include <QString>
#include <d3dcompiler.h>
#include "d3d11.h"

using Microsoft::WRL::ComPtr;



class DxCapture : public IDxToFrameSrc, public IDebugCollectable {
    ComPtr<ID3D11DeviceContext> _d3d11_deviceCtx = nullptr;
    ComPtr<ID3D11Device> _d3d11_device = nullptr;

    ComPtr<ID3D11SamplerState> _d3d11_samplerState = nullptr;
    ComPtr<ID3D11InputLayout> _d3d11_inputLayout = nullptr;
    ComPtr<ID3D11VertexShader> _d3d11_scale_vs = nullptr;
    ComPtr<ID3D11PixelShader> _d3d11_scale_ps = nullptr;
    ComPtr<ID3D11Buffer> _d3d11_vertexBuffer = nullptr;

    ComPtr<ID3D11RenderTargetView> _rtv_target_bgra = nullptr;

    ComPtr<ID3D11Texture2D> _texture_bgra = nullptr;
    HANDLE _texture_bgra_shared = nullptr;

    ID3D11Texture2D * _texture_captured = nullptr;

    ComPtr<ID3DBlob> _scale_vertex_shader = nullptr;
    ComPtr<ID3DBlob> _scale_pixel_shader = nullptr;

    std::unique_ptr<dx_capture_t> cap;
    std::atomic_bool _restartToSharedMemory = false;

    QMutex lock;

    std::atomic_bool _inited;

    uint32_t _width{0};
    uint32_t _height{0};

    uint32_t _capturedWidth{0};
    uint32_t _capturedHeigth{0};

    FpsCounter fps;

    bool createSharedSurf(int width, int height);
    void releaseSharedSurf();

    void initDxCapture();
    bool compileShader();
    void resetDeviceContext();

public:
    DxCapture(std::shared_ptr<DxToFrame> d3d = nullptr);
    ~DxCapture();

    QString debugInfo();

    bool init();

    void captureLock();
    void captureUnlock();
    void captureTick(float time);
    void capturedTexture(dx_texture_t* tex);

    bool copyTo(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *dest) override;
};

#endif //VTSLINK_D3D_CAPTURE_H
