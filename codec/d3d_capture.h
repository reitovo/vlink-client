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

class DxCapture : public IDxSrc, public IDebugCollectable {
    ComPtr<ID3D11DeviceContext> _d3d11_deviceCtx = nullptr;
    ComPtr<ID3D11Device> _d3d11_device = nullptr;
    ComPtr<ID3D11Texture2D> _texture_rgba = nullptr;

    HANDLE _texture_rgba_shared = nullptr;

    std::unique_ptr<dx_capture_t> cap;

    QMutex lock;

    std::atomic_bool _inited;

    uint32_t _width{ 0 };
    uint32_t _height{ 0 };

    FpsCounter fps;

public:
    DxCapture(std::shared_ptr<DxToFrame> d3d);
    ~DxCapture();

    QString debugInfo();

    bool init();
    bool createSharedSurf(int width, int height);
    void releaseSharedSurf();

    bool copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D *dest);
};

#endif //VTSLINK_D3D_CAPTURE_H
