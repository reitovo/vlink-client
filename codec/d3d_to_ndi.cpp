#include "d3d_to_ndi.h"
#include "QDebug"
#include "qdatetime.h"
#include "core/util.h"

#include <d3dcompiler.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxcore.h>
#include <DirectXMath.h>
#include <QFile>

extern "C" {
#include "libavutil/imgutils.h"
}

#pragma comment(lib, "d3dcompiler.lib")

#define NUMVERTICES 6

using DirectX::XMFLOAT3;
using DirectX::XMFLOAT2;

typedef struct _VERTEX
{
    XMFLOAT3 Pos;
    XMFLOAT2 TexCoord;
} VERTEX;

// Vertices for drawing whole texture
static VERTEX Vertices[NUMVERTICES] =
{
    {XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f)},
    {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
    {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
    {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
    {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
    {XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f)},
};

static FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };

DxToNdi::DxToNdi() {
    qDebug() << "begin d3d2ndi";
}

DxToNdi::~DxToNdi()
{
    qDebug() << "end d3d2ndi";
    lock.lock();

    _inited = false;
    releaseSharedSurf();

    COM_RESET(_d3d11_vertexBuffer);
    COM_RESET(_d3d11_pixelShader);
    COM_RESET(_d3d11_vertexShader);

    COM_RESET(_d3d11_inputLayout);
    COM_RESET(_d3d11_samplerState);
    COM_RESET(_d3d11_blendState);

    COM_RESET(_vertex_shader);
    COM_RESET(_pixel_shader);

    COM_RESET(_d3d11_deviceCtx);
    COM_RESET(_d3d11_device);

    lock.unlock();
    qDebug() << "end d3d2ndi done";
}

QString DxToNdi::debugInfo()
{
    auto count = 0;
    lock.lock();
    count = sources.count();
    lock.unlock();

    return QString("Dx->Ndi (NDI Generator) %1 Count: %2").arg(fps.stat()).arg(count);
}

void DxToNdi::registerSource(IDxSrc *src)
{
    qDebug() << "d3d2ndi register source";
    lock.lock();
    sources.append(src);
    lock.unlock();
}

void DxToNdi::unregisterSource(IDxSrc *src)
{
    qDebug() << "d3d2ndi unregister source";
    lock.lock();
    sources.removeOne(src);
    lock.unlock();
}

bool DxToNdi::compileShader()
{
    QFile f1(":/shader/blend_vertex.hlsl");
    f1.open(QIODevice::ReadOnly);
    auto s = QString(f1.readAll()).toStdString();
    HRESULT hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
                            "VS", "vs_5_0", 0, 0, _vertex_shader.GetAddressOf(), nullptr);
    if (FAILED(hr)) {
        qCritical() << "failed compiling blend vertex shader";
        return false;
    }

    QFile f2(":/shader/blend_pixel.hlsl");
    f2.open(QIODevice::ReadOnly);
    s = QString(f2.readAll()).toStdString();
    hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
                    "PS", "ps_5_0", 0, 0, _pixel_shader.GetAddressOf(), nullptr);
    if (FAILED(hr)) {
        qCritical() << "failed compiling blend pixel shader";
        return false;
    }

    qDebug() << "d3d2ndi compiled shader";
    return true;
}

bool DxToNdi::init()
{
    HRESULT hr;

    qDebug() << "d3d2ndi init";

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
            D3D11_CREATE_DEVICE_BGRA_SUPPORT ;

    if (IsDebuggerPresent()) {
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    IDXGIFactory2 *pDXGIFactory;
    IDXGIAdapter *pAdapter = NULL;
    hr = CreateDXGIFactory(IID_IDXGIFactory2, (void **)&pDXGIFactory);
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create dxgi factory";

    hr = pDXGIFactory->EnumAdapters(0, &pAdapter);
    if (FAILED(hr)) {
        qDebug() << "d3d2ndi failed enum adapter";
        return false;
    }

    DXGI_ADAPTER_DESC descAdapter;
    hr = pAdapter->GetDesc(&descAdapter);
    qDebug() << "using device" << QString::fromWCharArray(descAdapter.Description);

    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
    {
        hr = D3D11CreateDevice(pAdapter, DriverTypes[DriverTypeIndex], nullptr, creationFlags, FeatureLevels, NumFeatureLevels,
                               D3D11_SDK_VERSION, this->_d3d11_device.GetAddressOf(), &FeatureLevel, this->_d3d11_deviceCtx.GetAddressOf());
        if (SUCCEEDED(hr))
        {
            qDebug() << "d3d2ndi successfully created device";
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

    qDebug() << "d3d2ndi create sampler state";

    //VertexShader
    hr = this->_d3d11_device->CreateVertexShader(_vertex_shader->GetBufferPointer(), _vertex_shader->GetBufferSize(), nullptr, this->_d3d11_vertexShader.GetAddressOf());
    if (FAILED(hr)) {
        uint32_t err = HRESULT_CODE(hr);
        return false;
    }

    qDebug() << "d3d2ndi create vertex shader";

    constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> Layout =
    { {
          // 3D 32bit float vector
          { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
          // 2D 32bit float vector
          { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
      } };

    hr = this->_d3d11_device->CreateInputLayout(Layout.data(), Layout.size(),
                                                _vertex_shader->GetBufferPointer(), _vertex_shader->GetBufferSize(),
                                                this->_d3d11_inputLayout.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create input layout";

    //PixelShader
    hr = this->_d3d11_device->CreatePixelShader(_pixel_shader->GetBufferPointer(), _pixel_shader->GetBufferSize(), nullptr, this->_d3d11_pixelShader.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create pixel shader";

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

    qDebug() << "d3d2ndi create vertex buffer";

    auto width = 1920;
    auto height = 1080;
    auto ret = createSharedSurf(width, height);
    resetDeviceContext(width, height);

    _inited = true;
    qDebug() << "d3d2ndi init done";

    return ret;
}

void DxToNdi::resetDeviceContext(int width, int height)
{
    // alpha blend
    D3D11_BLEND_DESC blendDesc = {};
    auto& brt = blendDesc.RenderTarget[0];
    brt.BlendEnable = true;
    brt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    brt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    brt.BlendOp = D3D11_BLEND_OP_ADD;
    brt.SrcBlendAlpha = D3D11_BLEND_ONE;
    brt.DestBlendAlpha = D3D11_BLEND_ONE;
    brt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    brt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    _d3d11_device->CreateBlendState(&blendDesc, _d3d11_blendState.GetAddressOf());

    //init set
    this->_d3d11_deviceCtx->IASetInputLayout(this->_d3d11_inputLayout.Get());
    this->_d3d11_deviceCtx->OMSetBlendState(_d3d11_blendState.Get(), nullptr, 0xffffffff);
    //this->_d3d11_deviceCtx->ClearRenderTargetView(this->_renderTargetView.Get(), blendFactor);
    this->_d3d11_deviceCtx->VSSetShader(this->_d3d11_vertexShader.Get(), nullptr, 0);
    this->_d3d11_deviceCtx->PSSetShader(this->_d3d11_pixelShader.Get(), nullptr, 0);
    this->_d3d11_deviceCtx->PSSetSamplers(0, 1, this->_d3d11_samplerState.GetAddressOf());
    this->_d3d11_deviceCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT Stride = sizeof(VERTEX);
    UINT Offset = 0;
    this->_d3d11_deviceCtx->IASetVertexBuffers(0, 1, this->_d3d11_vertexBuffer.GetAddressOf(), &Stride, &Offset);

    //SharedSurf
    std::array<ID3D11ShaderResourceView*, 1> const textureViews = {
        this->_textureView.Get()
    };
    this->_d3d11_deviceCtx->PSSetShaderResources(0, textureViews.size(), textureViews.data());
    this->_d3d11_deviceCtx->OMSetRenderTargets(1, this->_renderTargetView.GetAddressOf(), nullptr);

    D3D11_VIEWPORT VP;
    VP.Width = static_cast<FLOAT>(width);
    VP.Height = static_cast<FLOAT>(height);
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    VP.TopLeftX = 0;
    VP.TopLeftY = 0;
    this->_d3d11_deviceCtx->RSSetViewports(1, &VP);
    //this->_d3d11_deviceCtx->Dispatch(8, 8, 1);
    this->_d3d11_deviceCtx->Dispatch(
                (UINT)ceil(width * 1.0 / 8),
                (UINT)ceil(height * 1.0 / 8),
                1);
    this->_width = width;
    this->_height = height;
}

bool DxToNdi::createSharedSurf(int width, int height)
{
    //
    HRESULT hr{ 0 };

    D3D11_TEXTURE2D_DESC texDesc_rgba;
    ZeroMemory(&texDesc_rgba, sizeof(texDesc_rgba));
    texDesc_rgba.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc_rgba.Width = width;
    texDesc_rgba.Height = height;
    texDesc_rgba.ArraySize = 1;
    texDesc_rgba.MipLevels = 1;
    texDesc_rgba.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc_rgba.Usage = D3D11_USAGE_DEFAULT;
    texDesc_rgba.CPUAccessFlags = 0;
    texDesc_rgba.SampleDesc.Count = 1;
    texDesc_rgba.SampleDesc.Quality = 0;
    texDesc_rgba.MiscFlags = 0;

    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_rgba_src.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create texture src";

    //
    D3D11_SHADER_RESOURCE_VIEW_DESC const srcDesc
            = CD3D11_SHADER_RESOURCE_VIEW_DESC(this->_texture_rgba_src.Get(), D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_B8G8R8A8_UNORM);
    hr = this->_d3d11_device->CreateShaderResourceView(this->_texture_rgba_src.Get(), &srcDesc, this->_textureView.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create shader resource view src";

    texDesc_rgba.BindFlags = D3D11_BIND_RENDER_TARGET;
    // Copy by ndi to av
    texDesc_rgba.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_rgba_target.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create texture target";

    IDXGIResource* pDXGIResource = NULL;
    _texture_rgba_target->QueryInterface(__uuidof(IDXGIResource), (LPVOID*) &pDXGIResource);
    pDXGIResource->GetSharedHandle(&_texture_rgba_target_handle);
    pDXGIResource->Release();
    if (!_texture_rgba_target_handle){
        return false;
    }

    qDebug() << "nv12bgra create texture target bgra shared handle";

    texDesc_rgba.BindFlags = 0;
    texDesc_rgba.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texDesc_rgba.Usage = D3D11_USAGE_STAGING;//cpu read
    texDesc_rgba.MiscFlags = 0;
    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_rgba_copy.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create texture copy cpu";

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    hr = this->_d3d11_device->CreateRenderTargetView(this->_texture_rgba_target.Get(), &rtvDesc, this->_renderTargetView.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2ndi create render target view";

    return true;
}

void DxToNdi::releaseSharedSurf()
{
    if (this->_d3d11_deviceCtx.Get() != nullptr)
        this->_d3d11_deviceCtx->ClearState();

    COM_RESET(_textureView);
    COM_RESET(_renderTargetView);

    COM_RESET(_texture_rgba_src);
    COM_RESET(_texture_rgba_target);
    COM_RESET(_texture_rgba_copy);

    this->_width = 0;
    this->_height = 0;
}

bool DxToNdi::mapNdi(NDIlib_video_frame_v2_t* frame)
{
    if (!_inited)
        return false;

    if (sources.empty())
        return false;

    if (_mapped)
        return false;

    QElapsedTimer t;
    t.start();

    //Elapsed e1("clear");

    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    this->_d3d11_deviceCtx->ClearRenderTargetView(_renderTargetView.Get(), clearColor);
    //e1.end();

    //Elapsed e2("render");
    int renderCount = 0;
    lock.lock();
    for (auto & src : sources) {
        if (!src->copyTo(_d3d11_device.Get(), _d3d11_deviceCtx.Get(), _texture_rgba_src.Get())) {
            continue;
        }
        this->_d3d11_deviceCtx->Draw(NUMVERTICES, 0);
        renderCount++;
    }
    lock.unlock();
    //e2.end();

    if (renderCount > 0) {
        //Elapsed e3("copy");
        this->_d3d11_deviceCtx->Flush();
        this->_d3d11_deviceCtx->CopyResource(this->_texture_rgba_copy.Get(), this->_texture_rgba_target.Get());
        //e3.end();

        //Elapsed e4("map");
        //get texture output
        //render target view only 1 sub resource https://docs.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-subresources
        D3D11_MAPPED_SUBRESOURCE ms;
        HRESULT hr = this->_d3d11_deviceCtx->Map(this->_texture_rgba_copy.Get(), /*SubResource*/ 0, D3D11_MAP_READ, 0, &ms);
        if (FAILED(hr))
            return false;
        //e4.end();

        frame->p_data = (uint8_t*)ms.pData;

        _mapped = true;
    }

    fps.add(t.nsecsElapsed());

    return true;
}

void DxToNdi::unmapNdi()
{
    if (!_mapped)
        return;

    this->_d3d11_deviceCtx->Unmap(this->_texture_rgba_copy.Get(), 0);
    _mapped = false;
}

bool DxToNdi::copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D *dest)
{
    if (!_inited)
        return false;

    lock.lock();

    ID3D11Texture2D *src;
    dev->OpenSharedResource(_texture_rgba_target_handle, __uuidof(ID3D11Texture2D), (LPVOID*) &src);
    ctx->CopyResource(dest, src);
    src->Release();
    ctx->Flush();

    lock.unlock();

    return true;
}
