//
// Created by reito on 2023/1/28.
//
#include "ui/windows/collabroom.h"
#include "d3d_capture.h"
#include "d3d_to_frame.h"
#include "QFile"
#include <dxgi1_2.h>
#include <dxcore.h>
#include <DirectXMath.h>
#include <d3d11.h>

#pragma comment(lib, "d3dcompiler.lib")

#define NUMVERTICES 6

using DirectX::XMFLOAT3;
using DirectX::XMFLOAT2;

typedef struct _VERTEX {
    XMFLOAT3 Pos;
    XMFLOAT2 TexCoord;
} VERTEX;

// Vertices for drawing whole texture
static VERTEX Vertices[NUMVERTICES] =
        {
                {XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f)},
                {XMFLOAT3(-1.0f, 1.0f, 0),  XMFLOAT2(0.0f, 0.0f)},
                {XMFLOAT3(1.0f, -1.0f, 0),  XMFLOAT2(1.0f, 1.0f)},
                {XMFLOAT3(1.0f, -1.0f, 0),  XMFLOAT2(1.0f, 1.0f)},
                {XMFLOAT3(-1.0f, 1.0f, 0),  XMFLOAT2(0.0f, 0.0f)},
                {XMFLOAT3(1.0f, 1.0f, 0),   XMFLOAT2(1.0f, 0.0f)},
        };

static FLOAT blendFactor[4] = {0.f, 0.f, 0.f, 0.f};

static void dx_lock(void *user) {
    DxCapture *dx = static_cast<DxCapture *>(user);
    dx->captureLock();
}

static void dx_unlock(void *user) {
    DxCapture *dx = static_cast<DxCapture *>(user);
    dx->captureUnlock();
}

static char *dx_get_file_path(const char *filename) {
    char *ret = (char *) _aligned_malloc(512, 32);
    sprintf(ret, IsDebuggerPresent() ? R"(E:\VTSLink\Client\VTSLink\out\build\x64-Debug\%s)" : "./%s", filename);
    return ret;
}

static void dx_captured_texture(void *user, dx_texture_t *tex) {
    DxCapture *dx = static_cast<DxCapture *>(user);
    dx->capturedTexture(tex);
}

DxCapture::DxCapture(std::shared_ptr<DxToFrame> d3d) : IDxToFrameSrc(d3d) {
    qDebug() << "begin d3d capture";
    if (d3d != nullptr) {
        d3d->registerSource(this);
        qDebug() << "running at server mode";
    }

    init();
}

DxCapture::~DxCapture() {
    qDebug() << "end d3d capture";
    if (CollabRoom::instance() != nullptr)
            emit CollabRoom::instance()->onDxgiCaptureStatus("idle");

    if (d3d != nullptr) {
        d3d->unregisterSource(this);
    }

    if (cap != nullptr) {
        dx_capture_destroy(cap.get());
        cap.reset();
    }

    lock.lock();

    releaseSharedSurf();

    COM_RESET(_d3d11_deviceCtx);
    COM_RESET(_d3d11_device);

    _inited = false;

    lock.unlock();
    qDebug() << "end d3d capture done";
}

bool DxCapture::compileShader() {
    QFile f1(":/shader/scale_vertex.hlsl");
    f1.open(QIODevice::ReadOnly);
    auto s = QString(f1.readAll()).toStdString();
    HRESULT hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
                            "VS", "vs_5_0", 0, 0, _scale_vertex_shader.GetAddressOf(), nullptr);
    if (FAILED(hr)) {
        qCritical() << "failed compiling scale vertex shader";
        return false;
    }

    QFile f2(":/shader/scale_pixel.hlsl");
    f2.open(QIODevice::ReadOnly);
    s = QString(f2.readAll()).toStdString();
    hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
                    "PS", "ps_5_0", 0, 0, _scale_pixel_shader.GetAddressOf(), nullptr);
    if (FAILED(hr)) {
        qCritical() << "failed compiling scale pixel shader";
        return false;
    }

    qDebug() << "d3d capture compile shader done";
    return true;
}

bool DxCapture::init() {
    HRESULT hr;

    auto width = VTSLINK_FRAME_WIDTH;
    auto height = VTSLINK_FRAME_HEIGHT;
    _width = width;
    _height = height;

    qDebug() << "dx capture init";

    compileShader();

    // Driver types supported
    D3D_DRIVER_TYPE DriverTypes[] =
            {
                    D3D_DRIVER_TYPE_UNKNOWN,
                    //D3D_DRIVER_TYPE_HARDWARE,
                    //D3D_DRIVER_TYPE_WARP,
                    //D3D_DRIVER_TYPE_REFERENCE,
            };
    UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

    // Feature levels supported
    D3D_FEATURE_LEVEL FeatureLevels[] =
            {
                    D3D_FEATURE_LEVEL_11_0,
            };
    UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);
    D3D_FEATURE_LEVEL FeatureLevel;
    // This flag adds support for surfaces with a different color channel ordering
    // than the default. It is required for compatibility with Direct2D.
    UINT creationFlags =
            D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    if (IsDebuggerPresent() && DX_DEBUG_LAYER) {
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    // We need dxgi to share texture
    IDXGIFactory2 *pDXGIFactory;
    IDXGIAdapter *pAdapter = NULL;
    hr = CreateDXGIFactory(IID_IDXGIFactory2, (void **) &pDXGIFactory);
    if (FAILED(hr))
        return false;

    qDebug() << "dx capture create dxgi factory";

    hr = pDXGIFactory->EnumAdapters(0, &pAdapter);
    if (FAILED(hr))
        return false;

    DXGI_ADAPTER_DESC descAdapter;
    hr = pAdapter->GetDesc(&descAdapter);

    qDebug() << "using device" << QString::fromWCharArray(descAdapter.Description);

    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex) {
        hr = D3D11CreateDevice(pAdapter, DriverTypes[DriverTypeIndex], nullptr, creationFlags, FeatureLevels, NumFeatureLevels,
                               D3D11_SDK_VERSION, this->_d3d11_device.GetAddressOf(), &FeatureLevel, this->_d3d11_deviceCtx.GetAddressOf());
        if (SUCCEEDED(hr)) {
            qDebug() << "dx capture create device successfully";
            // Device creation succeeded, no need to loop anymore
            break;
        }
    }
    if (FAILED(hr))
        return false;

    pDXGIFactory->Release();
    pAdapter->Release();

    //SamplerState
    D3D11_SAMPLER_DESC desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
    hr = this->_d3d11_device->CreateSamplerState(&desc, this->_d3d11_samplerState.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "dx capture create sampler state";

    //VertexShader
    hr = this->_d3d11_device->CreateVertexShader(_scale_vertex_shader->GetBufferPointer(), _scale_vertex_shader->GetBufferSize(),
                                                 nullptr, this->_d3d11_scale_vs.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "dx capture create vertex shader";

    constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> Layout =
            {{
                     // 3D 32bit float vector
                     {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                     // 2D 32bit float vector
                     {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
             }};

    //Once an input-layout object is created from a shader signature, the input-layout object
    //can be reused with any other shader that has an identical input signature (semantics included).
    //This can simplify the creation of input-layout objects when you are working with many shaders with identical inputs.
    //https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout
    hr = this->_d3d11_device->CreateInputLayout(Layout.data(), Layout.size(),
                                                _scale_vertex_shader->GetBufferPointer(), _scale_vertex_shader->GetBufferSize(),
                                                this->_d3d11_inputLayout.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "dx capture create input layout";

    //PixelShader
    hr = this->_d3d11_device->CreatePixelShader(_scale_pixel_shader->GetBufferPointer(), _scale_pixel_shader->GetBufferSize(), nullptr, this->_d3d11_scale_ps.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "dx capture create pixel shader";

    //VertexBuffer
    D3D11_BUFFER_DESC BufferDesc;
    RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
    BufferDesc.Usage = D3D11_USAGE_DEFAULT;
    BufferDesc.ByteWidth = sizeof(VERTEX) * NUMVERTICES;
    BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    BufferDesc.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = Vertices;
    hr = this->_d3d11_device->CreateBuffer(&BufferDesc, &InitData, this->_d3d11_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "dx capture create vertex buffer";

    auto ret = createSharedSurf(width, height);
    resetDeviceContext();

    initDxCapture();

    _inited = true;

    qDebug() << "dx capture init done";

    return ret;
}

bool DxCapture::createSharedSurf(int width, int height) {
    HRESULT hr{0};

    D3D11_TEXTURE2D_DESC texDesc_rgba;
    ZeroMemory(&texDesc_rgba, sizeof(texDesc_rgba));
    texDesc_rgba.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc_rgba.Width = width;
    texDesc_rgba.Height = height;
    texDesc_rgba.ArraySize = 1;
    texDesc_rgba.MipLevels = 1;
    texDesc_rgba.BindFlags = D3D11_BIND_RENDER_TARGET;
    texDesc_rgba.Usage = D3D11_USAGE_DEFAULT;
    texDesc_rgba.CPUAccessFlags = 0;
    texDesc_rgba.SampleDesc.Count = 1;
    texDesc_rgba.SampleDesc.Quality = 0;
    texDesc_rgba.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_bgra.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "dx capture create texture bgra";

    // shared texture
    IDXGIResource *pDXGIResource = NULL;
    _texture_bgra->QueryInterface(__uuidof(IDXGIResource), (LPVOID *) &pDXGIResource);
    pDXGIResource->GetSharedHandle(&_texture_bgra_shared);
    pDXGIResource->Release();
    if (!_texture_bgra_shared) {
        return false;
    }

    qDebug() << "dx capture create texture shared handle";

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    hr = this->_d3d11_device->CreateRenderTargetView(this->_texture_bgra.Get(), &rtvDesc, this->_rtv_target_bgra.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create render target view";

    return true;
}

void DxCapture::releaseSharedSurf() {
    if (this->_d3d11_deviceCtx.Get() != nullptr)
        this->_d3d11_deviceCtx->ClearState();

    COM_RESET(_texture_bgra);
}

bool DxCapture::copyTo(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *dest) {
    if (!_inited)
        return false;

    lock.lock();

    QElapsedTimer t;
    t.start();

    if (_texture_captured != nullptr) {
        FLOAT clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        this->_d3d11_deviceCtx->ClearRenderTargetView(_rtv_target_bgra.Get(), clearColor);
        this->_d3d11_deviceCtx->Draw(NUMVERTICES, 0);
        this->_d3d11_deviceCtx->Flush();
    }

    ID3D11Texture2D *src;
    dev->OpenSharedResource(_texture_bgra_shared, __uuidof(ID3D11Texture2D), (LPVOID *) &src);
    if (src == nullptr)
        return false;

    D3D11_BOX box = {0, 0, 0, _width, _height, 1};
    ctx->CopySubresourceRegion(dest, 0, 0, 0, 0, src, 0, &box);
    ctx->Flush();

    src->Release();

    fps.add(t.nsecsElapsed());

    lock.unlock();

    return true;
}

QString DxCapture::debugInfo() {
    return QString("Dx->Dx (D3D11 Capture) %1").arg(fps.stat());
}

void DxCapture::initDxCapture() {
    cap = std::make_unique<dx_capture_t>();
    memset(cap.get(), 0, sizeof(dx_capture_t));

    cap->device = _d3d11_device.Get();
    cap->context = _d3d11_deviceCtx.Get();
    cap->user = this;
    cap->lock = dx_lock;
    cap->unlock = dx_unlock;
    cap->on_get_hook_file_path = dx_get_file_path;
    cap->on_captured_texture = dx_captured_texture;

    dx_capture_setting_default(&cap->setting);
    cap->setting.window = "VTube Studio:UnityWndClass:VTube Studio.exe";
    cap->setting.force_shmem = _restartToSharedMemory;
    _isShareMemory = cap->setting.force_shmem;

    dx_capture_init(cap.get());
    emit CollabRoom::instance()->onDxgiCaptureStatus("init");
}

void DxCapture::captureLock() {
    lock.lock();
}

void DxCapture::captureUnlock() {
    lock.unlock();
}

void DxCapture::captureTick(float time) {
    if (_restartToSharedMemory) {
        qDebug() << "restart to shmem";
        dx_capture_destroy(cap.get());
        cap.reset();
        initDxCapture();
        _restartToSharedMemory = false;

        emit CollabRoom::instance()->onDowngradedToSharedMemory();
        return;
    }
    dx_capture_tick(cap.get(), time);
}

void DxCapture::capturedTexture(dx_texture_t *tex) {
    _texture_captured = tex;

    static int failureCount = 0;

    if (tex != nullptr) {
        failureCount = 0;
        D3D11_TEXTURE2D_DESC td = {};
        tex->GetDesc(&td);
        qDebug() << "captured texture" << td.Width << td.Height;

        auto type = td.Format;
        if (type == DXGI_FORMAT_R8G8B8A8_TYPELESS) {
            type = DXGI_FORMAT_R8G8B8A8_UNORM;
            qDebug() << "format rgba";
        } else if (type == DXGI_FORMAT_B8G8R8A8_TYPELESS) {
            type = DXGI_FORMAT_B8G8R8A8_UNORM;
            qDebug() << "format bgra";
        }

        ComPtr<ID3D11ShaderResourceView> _capturedView = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC const srcDesc
                = CD3D11_SHADER_RESOURCE_VIEW_DESC(_texture_captured, D3D11_SRV_DIMENSION_TEXTURE2D, type);
        HRESULT hr = this->_d3d11_device->CreateShaderResourceView(_texture_captured, &srcDesc, _capturedView.GetAddressOf());
        if (FAILED(hr)) {
            qDebug() << "captured texture failed create shader resource view";
        }

        std::array<ID3D11ShaderResourceView *, 1> const textureViews = {
                _capturedView.Get()
        };
        this->_d3d11_deviceCtx->PSSetShaderResources(0, textureViews.size(), textureViews.data());

        emit CollabRoom::instance()->onDxgiCaptureStatus(_isShareMemory ? "shmem" : "shtex");
    } else {
        _capturedHeigth = _capturedWidth = 0;
        qDebug() << "null captured texture";

        if (++failureCount == 3) {
            if (_isShareMemory) {
                emit CollabRoom::instance()->onDxgiCaptureStatus("fail");
                qDebug() << "shmem also failed";
            } else {
                failureCount = 0;
                cap->setting.force_shmem = true;
                _restartToSharedMemory = true;
            }
        }
    }
}

void DxCapture::resetDeviceContext() {
    //init set
    this->_d3d11_deviceCtx->IASetInputLayout(this->_d3d11_inputLayout.Get());
    this->_d3d11_deviceCtx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    //this->_d3d11_deviceCtx->ClearRenderTargetView(this->_renderTargetView.Get(), blendFactor);
    this->_d3d11_deviceCtx->VSSetShader(this->_d3d11_scale_vs.Get(), nullptr, 0);
    this->_d3d11_deviceCtx->PSSetShader(this->_d3d11_scale_ps.Get(), nullptr, 0);
    this->_d3d11_deviceCtx->PSSetSamplers(0, 1, this->_d3d11_samplerState.GetAddressOf());
    this->_d3d11_deviceCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT Stride = sizeof(VERTEX);
    UINT Offset = 0;
    this->_d3d11_deviceCtx->IASetVertexBuffers(0, 1, this->_d3d11_vertexBuffer.GetAddressOf(), &Stride, &Offset);

    qDebug() << "dx capture set context params";

    //SharedSurf
    this->_d3d11_deviceCtx->OMSetRenderTargets(1, this->_rtv_target_bgra.GetAddressOf(), nullptr);

    qDebug() << "dx capture set context textures";

    D3D11_VIEWPORT VP;
    VP.Width = static_cast<FLOAT>(_width);
    VP.Height = static_cast<FLOAT>(_height);
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    VP.TopLeftX = 0;
    VP.TopLeftY = 0;
    this->_d3d11_deviceCtx->RSSetViewports(1, &VP);
    //this->_d3d11_deviceCtx->Dispatch(8, 8, 1);
    this->_d3d11_deviceCtx->Dispatch(
            (UINT) ceil(_width * 1.0 / 8),
            (UINT) ceil(_height * 2 * 1.0 / 8),
            1);

    qDebug() << "dx capture set context viewport";
}
