#include "nv12_to_bgra.h"
#include "core/util.h"
#include <d3d11.h>
#include <dxcore.h>
#include <DirectXMath.h>
#include <QDebug>
#include <QFile>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

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

Nv12ToBgra::Nv12ToBgra(int width, int height) {
    qDebug() << "begin nv12bgra";

    _width = width;
    _height = height;
}

Nv12ToBgra::~Nv12ToBgra() {
    qDebug() << "end nv12bgra";

    lock.lock();

    releaseSharedSurf();

    COM_RESET(_d3d11_vertexBuffer);
    COM_RESET(_d3d11_pixelShader);
    COM_RESET(_d3d11_inputLayout);
    COM_RESET(_d3d11_vertexShader);
    COM_RESET(_d3d11_samplerState);

    COM_RESET(_vertex_shader);
    COM_RESET(_pixel_shader);

    COM_RESET(_d3d11_deviceCtx);
    printDxLiveObjects(_d3d11_device.Get(), __FUNCTION__);
    COM_RESET(_d3d11_device);

    lock.unlock();
    qDebug() << "end nv12bgra done";
}

bool Nv12ToBgra::compileShader() {
    ID3DBlob *errs;

    QFile f1(":/shader/nv12bgra_vertex.hlsl");
    f1.open(QIODevice::ReadOnly);
    auto s = QString(f1.readAll()).toStdString();
    HRESULT hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
                            "VS", "vs_5_0", 0, 0, _vertex_shader.GetAddressOf(), nullptr);
    if (FAILED(hr)) {
        qCritical() << "failed compiling nv12bgra vertex shader";
        return false;
    }

    QFile f2(":/shader/nv12bgra_pixel.hlsl");
    f2.open(QIODevice::ReadOnly);
    s = QString(f2.readAll()).toStdString();
    hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
                    "PS", "ps_5_0", 0, 0, _pixel_shader.GetAddressOf(), &errs);
    if (FAILED(hr)) {
        qCritical() << "failed compiling nv12bgra pixel shader";
        auto e = std::string((char *) errs->GetBufferPointer(), errs->GetBufferSize());
        qCritical("%s", e.c_str());
        return false;
    }

    qDebug() << "nv12bgra compile shader done";
    return true;
}

void Nv12ToBgra::releaseSharedSurf() {
    if (this->_d3d11_deviceCtx.Get() != nullptr)
        this->_d3d11_deviceCtx->ClearState();

    COM_RESET(_chrominanceView);
    COM_RESET(_luminanceView);

    COM_RESET(_texture_nv12);
    COM_RESET(_texture_rgba_target);
    COM_RESET(_texture_rgba_copy);

    COM_RESET(_renderTargetView);

    this->_width = 0;
    this->_height = 0;
}

ID3D11Device *Nv12ToBgra::getDevice() {
    _d3d11_device->AddRef();
    return _d3d11_device.Get();
}

bool Nv12ToBgra::init() {
    HRESULT hr;

    qDebug() << "nv12bgra init";

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
            D3D11_CREATE_DEVICE_SINGLETHREADED |
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

    qDebug() << "nv12bgra create dxgi factory";

    hr = pDXGIFactory->EnumAdapters(0, &pAdapter);
    if (FAILED(hr))
        return false;

    DXGI_ADAPTER_DESC descAdapter;
    hr = pAdapter->GetDesc(&descAdapter);
    qDebug() << "using device" << QString::fromWCharArray(descAdapter.Description);

    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex) {
        hr = D3D11CreateDevice(pAdapter, DriverTypes[DriverTypeIndex], nullptr, creationFlags, FeatureLevels,
                               NumFeatureLevels,
                               D3D11_SDK_VERSION, this->_d3d11_device.GetAddressOf(), &FeatureLevel,
                               this->_d3d11_deviceCtx.GetAddressOf());
        if (SUCCEEDED(hr)) {
            qDebug() << "nv12bgra d3d11 create device success";
            // Device creation succeeded, no need to loop anymore
            break;
        }
    }
    if (FAILED(hr)) {
        qDebug() << "nv12bgra d3d11 create device failed" << HRESULT_CODE(hr);
        return false;
    }

    pDXGIFactory->Release();
    pAdapter->Release();

    //SamplerState
    D3D11_SAMPLER_DESC desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
    hr = this->_d3d11_device->CreateSamplerState(&desc, this->_d3d11_samplerState.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create sampler state";

    //VertexShader
    hr = this->_d3d11_device->CreateVertexShader(_vertex_shader->GetBufferPointer(), _vertex_shader->GetBufferSize(),
                                                 nullptr, this->_d3d11_vertexShader.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create vertex shader";

    constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> Layout =
            {
                    {
                            // 3D 32bit float vector
                            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                            // 2D 32bit float vector
                            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    }
            };

    hr = this->_d3d11_device->CreateInputLayout(Layout.data(), Layout.size(),
                                                _vertex_shader->GetBufferPointer(), _vertex_shader->GetBufferSize(),
                                                this->_d3d11_inputLayout.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create input layout";

    //PixelShader
    hr = this->_d3d11_device->CreatePixelShader(_pixel_shader->GetBufferPointer(), _pixel_shader->GetBufferSize(),
                                                nullptr, this->_d3d11_pixelShader.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create pixel shader";

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

    qDebug() << "nv12bgra create vertex buffer";

    auto ret = createSharedSurf();
    resetDeviceContext();

    _inited = true;

    qDebug() << "nv12bgra init done";
    return ret;
}

void Nv12ToBgra::resetDeviceContext() {
    //init set
    this->_d3d11_deviceCtx->IASetInputLayout(this->_d3d11_inputLayout.Get());
    this->_d3d11_deviceCtx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    //this->_d3d11_deviceCtx->ClearRenderTargetView(this->_renderTargetView.Get(), blendFactor);
    this->_d3d11_deviceCtx->VSSetShader(this->_d3d11_vertexShader.Get(), nullptr, 0);
    this->_d3d11_deviceCtx->PSSetShader(this->_d3d11_pixelShader.Get(), nullptr, 0);
    this->_d3d11_deviceCtx->PSSetSamplers(0, 1, this->_d3d11_samplerState.GetAddressOf());
    this->_d3d11_deviceCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT Stride = sizeof(VERTEX);
    UINT Offset = 0;
    this->_d3d11_deviceCtx->IASetVertexBuffers(0, 1, this->_d3d11_vertexBuffer.GetAddressOf(), &Stride, &Offset);

    qDebug() << "nv12bgra set context params";

    //SharedSurf
    std::array<ID3D11ShaderResourceView *, 2> const textureViews = {
            this->_luminanceView.Get(),
            this->_chrominanceView.Get()
    };
    this->_d3d11_deviceCtx->PSSetShaderResources(0, textureViews.size(), textureViews.data());
    this->_d3d11_deviceCtx->OMSetRenderTargets(1, this->_renderTargetView.GetAddressOf(), nullptr);

    qDebug() << "nv12bgra set context textures";

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

    qDebug() << "nv12bgra set context viewport";
}

bool Nv12ToBgra::createSharedSurf() {
    //
    HRESULT hr{0};

    D3D11_TEXTURE2D_DESC texDesc_nv12;
    ZeroMemory(&texDesc_nv12, sizeof(texDesc_nv12));
    texDesc_nv12.Format = DXGI_FORMAT_NV12;
    texDesc_nv12.Width = _width;
    texDesc_nv12.Height = _height * 2;
    texDesc_nv12.ArraySize = 1;
    texDesc_nv12.MipLevels = 1;
    texDesc_nv12.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc_nv12.Usage = D3D11_USAGE_DEFAULT;
    texDesc_nv12.CPUAccessFlags = 0;
    texDesc_nv12.SampleDesc.Count = 1;
    texDesc_nv12.SampleDesc.Quality = 0;
    texDesc_nv12.MiscFlags = 0;

    hr = this->_d3d11_device->CreateTexture2D(&texDesc_nv12, nullptr, this->_texture_nv12.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create texture nv12 rgb";

    std::string name = "nv12bgra nv12 rgb";
    _texture_nv12->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

    //
    D3D11_SHADER_RESOURCE_VIEW_DESC const luminancePlaneDesc
            = CD3D11_SHADER_RESOURCE_VIEW_DESC(this->_texture_nv12.Get(), D3D11_SRV_DIMENSION_TEXTURE2D,
                                               DXGI_FORMAT_R8_UNORM);
    hr = this->_d3d11_device->CreateShaderResourceView(this->_texture_nv12.Get(), &luminancePlaneDesc,
                                                       this->_luminanceView.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create shader resview nv12 rgb l";

    //
    D3D11_SHADER_RESOURCE_VIEW_DESC const chrominancePlaneDesc
            = CD3D11_SHADER_RESOURCE_VIEW_DESC(this->_texture_nv12.Get(), D3D11_SRV_DIMENSION_TEXTURE2D,
                                               DXGI_FORMAT_R8G8_UNORM);
    hr = this->_d3d11_device->CreateShaderResourceView(this->_texture_nv12.Get(), &chrominancePlaneDesc,
                                                       this->_chrominanceView.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create shader resview nv12 rgb c";

    D3D11_TEXTURE2D_DESC texDesc_rgba;
    ZeroMemory(&texDesc_rgba, sizeof(texDesc_rgba));
    texDesc_rgba.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc_rgba.Width = _width;
    texDesc_rgba.Height = _height;
    texDesc_rgba.ArraySize = 1;
    texDesc_rgba.MipLevels = 1;
    texDesc_rgba.BindFlags = D3D11_BIND_RENDER_TARGET;
    texDesc_rgba.Usage = D3D11_USAGE_DEFAULT;
    texDesc_rgba.CPUAccessFlags = 0;
    texDesc_rgba.SampleDesc.Count = 1;
    texDesc_rgba.SampleDesc.Quality = 0;
    texDesc_rgba.MiscFlags = 0;
    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_rgba_target.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create texture target bgra";

    name = "nv12bgra rgba target";
    _texture_rgba_target->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

    // shared texture
    texDesc_rgba.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_rgba_copy.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create texture copy bgra";

    IDXGIResource *pDXGIResource = NULL;
    _texture_rgba_copy->QueryInterface(__uuidof(IDXGIResource), (LPVOID *) &pDXGIResource);
    pDXGIResource->GetSharedHandle(&_texture_rgba_copy_shared);
    pDXGIResource->Release();
    if (!_texture_rgba_copy_shared) {
        return false;
    }

    qDebug() << "nv12bgra create texture target bgra shared handle";

    name = "nv12bgra rgba copy";
    _texture_rgba_copy->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    hr = this->_d3d11_device->CreateRenderTargetView(this->_texture_rgba_target.Get(), &rtvDesc,
                                                     this->_renderTargetView.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "nv12bgra create render target view";

    return true;
}

bool Nv12ToBgra::nv12ToBgra(AVFrame *f) {
    HRESULT hr{0};

    if (!_inited) {
        return false;
    }

    if (f->format != AV_PIX_FMT_D3D11)
        return false;
    if (!f->hw_frames_ctx)
        return false;

    D3D11_BOX srcBox = {0, 0, 0, _width, _height * 2, 1};

    ID3D11Texture2D *textureRgb = (ID3D11Texture2D *) f->data[0];
    const int textureRgbIndex = (int) f->data[1];
    //qDebug() << "copy rgb" << textureRgb << textureRgbIndex;

    //bind/copy ffmpeg hw texture -> local d3d11 texture
    this->_d3d11_deviceCtx->CopySubresourceRegion(
            this->_texture_nv12.Get(), 0, 0, 0, 0,
            textureRgb, textureRgbIndex, &srcBox
    );

    //saveTextureToFile(_d3d11_deviceCtx.Get(), _texture_nv12.Get(), "./nv12_bgra_input.png");

    //    qDebug() << "clear";
    //    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    //    this->_d3d11_deviceCtx->ClearRenderTargetView(_renderTargetView.Get(), clearColor);

    //qDebug() << "draw";
    this->_d3d11_deviceCtx->Draw(NUMVERTICES, 0);

    lock.lock();

    //qDebug() << "copy to buffer";
    this->_d3d11_deviceCtx->CopyResource(this->_texture_rgba_copy.Get(), this->_texture_rgba_target.Get());
    this->_d3d11_deviceCtx->Flush();
    //showTexture(this->_d3d11_deviceCtx.Get(), _texture_rgba_copy.Get(), "");

    lock.unlock();

    return true;
}

bool Nv12ToBgra::copyTo(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *dest) {
    if (!_inited)
        return false;

    //qDebug() << "copy from buffer";

    lock.lock();

    ID3D11Texture2D *src;
    dev->OpenSharedResource(_texture_rgba_copy_shared, __uuidof(ID3D11Texture2D), (LPVOID *) &src);
    if (src != nullptr) {
        D3D11_BOX box = {0, 0, 0, _width, _height, 1};
        ctx->CopySubresourceRegion(dest, 0, 0, 0, 0, src, 0, &box);
        ctx->Flush();
        src->Release();
    }

    lock.unlock();

    return true;
}

ID3D11Texture2D *Nv12ToBgra::getSharedTargetTexture(ID3D11Device *dev, ID3D11DeviceContext *ctx) {
    if (!_inited)
        return nullptr;

    if (!_texture_rgba_copy_shared)
        return nullptr;

    lock.lock();

    ID3D11Texture2D *src;
    dev->OpenSharedResource(_texture_rgba_copy_shared, __uuidof(ID3D11Texture2D), (LPVOID *) &src);

    lock.unlock();

    return src;
}

int DxFrameBuffer::size() const {
    return queue.size();
}

ComPtr<ID3D11Texture2D> DxFrameBuffer::getFree() {
    DxFrame ret;
    if (!free.empty()) {
        ret = free.front();
        free.pop();
    } else if (!queue.empty()) {
        ret = queue.front();
        queue.pop();
    }
    return ret.frame;
}

void DxFrameBuffer::addFree(ComPtr<ID3D11Texture2D> f) {
    free.push({-1, f});
}

ComPtr<ID3D11Texture2D> DxFrameBuffer::dequeue() {
    if (queue.empty())
        return nullptr;

    DxFrame ret;
    ret = queue.front();
    queue.pop();
    return ret.frame;
}

void DxFrameBuffer::enqueue(ComPtr<ID3D11Texture2D> f, int64_t pts) {
    queue.push({pts, f});
}

int64_t DxFrameBuffer::topPts() {
    return queue.front().pts;
}

void DxFrameBuffer::clear() {
    while (!queue.empty()) {
        auto t = queue.front();
        queue.pop();
        free.push({-1, t.frame});
    }
}
