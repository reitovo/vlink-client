#include "d3d_to_frame.h"
#include "QDebug"
#include "qdatetime.h"
#include "core/util.h"
#include "ui/windows/dxgioutput.h"

#include <d3dcompiler.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dxcore.h>
#include <DirectXMath.h>
#include <QFile>
#include <dxgidebug.h>
#include <comdef.h>

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

DxToFrame::DxToFrame(int width, int height) {
    qDebug() << "begin d3d2dx";
    _width = width;
    _height = height;
}

DxToFrame::~DxToFrame()
{
    qDebug() << "end d3d2dx";
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

    COM_RESET(_swap_chain_back_buffer);
    COM_RESET(_swap_chain);

    COM_RESET(_d3d11_deviceCtx);
    printDxLiveObjects(_d3d11_device.Get(), __FUNCTION__);
    COM_RESET(_d3d11_device);

    lock.unlock();
    qDebug() << "end d3d2dx done";
}

QString DxToFrame::debugInfo()
{
    auto count = 0;
    lock.lock();
    count = sources.count();
    lock.unlock();

    return QString("Dx->Frame (D3D11 Render) %1 Sources: %2")
        .arg(renderFps.stat()).arg(count);
}

void DxToFrame::registerSource(IDxToFrameSrc *src)
{
    qDebug() << "d3d2dx register source";
    lock.lock();
    sources.append(src);
    lock.unlock();
}

void DxToFrame::unregisterSource(IDxToFrameSrc *src)
{
    qDebug() << "d3d2dx unregister source";
    lock.lock();
    sources.removeOne(src);
    lock.unlock();
}

bool DxToFrame::compileShader()
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

    qDebug() << "d3d2dx compiled shader";
    return true;
}

bool DxToFrame::init(bool swap)
{
    HRESULT hr;

    qDebug() << "d3d2dx init";

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
            //D3D11_CREATE_DEVICE_SINGLETHREADED | Make OBS hookable
            D3D11_CREATE_DEVICE_BGRA_SUPPORT ;

    if (IsDebuggerPresent() && DX_DEBUG_LAYER) {
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG; 
    }

    IDXGIFactory3 *pDXGIFactory;
    IDXGIAdapter *pAdapter = nullptr;
    auto dxgiCreateFlag = 0;
    if (IsDebuggerPresent() && DX_DEBUG_LAYER) {
        dxgiCreateFlag |= DXGI_CREATE_FACTORY_DEBUG;
    }
    hr = CreateDXGIFactory2(dxgiCreateFlag, IID_IDXGIFactory3, (void **)&pDXGIFactory);
    if (FAILED(hr))
        return false;
    qDebug() << "d3d2dx create dxgi factory";

    hr = pDXGIFactory->EnumAdapters(0, &pAdapter);
    if (FAILED(hr)) {
        qDebug() << "d3d2dx failed enum adapter";
        return false;
    }

    DXGI_ADAPTER_DESC descAdapter;
    pAdapter->GetDesc(&descAdapter);
    qDebug() << "using device" << QString::fromWCharArray(descAdapter.Description);

    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
    {
        hr = D3D11CreateDevice(pAdapter, DriverTypes[DriverTypeIndex], nullptr, creationFlags, FeatureLevels, NumFeatureLevels,
                               D3D11_SDK_VERSION, this->_d3d11_device.GetAddressOf(), &FeatureLevel,
                               this->_d3d11_deviceCtx.GetAddressOf());
        if (SUCCEEDED(hr))
        {
            qDebug() << "d3d2dx successfully created device";
            // Device creation succeeded, no need to loop anymore
            break;
        }
    }
    if (FAILED(hr))
        return false;

    if (swap) {
        auto hwnd = DxgiOutput::getHwnd();

        DXGI_SWAP_CHAIN_DESC1 desc1 = {};
        desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc1.Scaling = DXGI_SCALING_STRETCH;
        desc1.Height = _height;
        desc1.Width = _width;
        desc1.BufferCount = 2;
        desc1.SampleDesc.Quality = 0;
        desc1.SampleDesc.Count = 1;
        desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        ComPtr<IDXGISwapChain1> sw1;
        hr = pDXGIFactory->CreateSwapChainForHwnd(_d3d11_device.Get(), hwnd, &desc1, nullptr, nullptr, sw1.GetAddressOf());
        if (FAILED(hr)) {
            qDebug() << "failed to create swapchain1";
            return false;
        }

        hr = sw1->QueryInterface(__uuidof(IDXGISwapChain2), (void**) _swap_chain.GetAddressOf());
        if (FAILED(hr)) {
            qDebug() << "failed to create swapchain2";
            return false;
        }

        hr = _swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)_swap_chain_back_buffer.GetAddressOf());
        if (FAILED(hr)) {
            qDebug() << "failed to get swapchain backbuffer";
            return false;
        } 
    }

    pDXGIFactory->Release();
    pAdapter->Release();

    //SamplerState
    D3D11_SAMPLER_DESC desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
    hr = this->_d3d11_device->CreateSamplerState(&desc, this->_d3d11_samplerState.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2dx create sampler state";

    //VertexShader
    hr = this->_d3d11_device->CreateVertexShader(_vertex_shader->GetBufferPointer(), _vertex_shader->GetBufferSize(), nullptr, this->_d3d11_vertexShader.GetAddressOf());
    if (FAILED(hr)) {
        uint32_t err = HRESULT_CODE(hr);
        return false;
    }

    qDebug() << "d3d2dx create vertex shader";

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

    qDebug() << "d3d2dx create input layout";

    //PixelShader
    hr = this->_d3d11_device->CreatePixelShader(_pixel_shader->GetBufferPointer(), _pixel_shader->GetBufferSize(), nullptr, this->_d3d11_pixelShader.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2dx create pixel shader";

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

    qDebug() << "d3d2dx create vertex buffer";

    auto ret = createSharedSurf();
    resetDeviceContext();

    _inited = true;
    qDebug() << "d3d2dx init done";

    return ret;
}

void DxToFrame::resetDeviceContext()
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
    VP.Width = static_cast<FLOAT>(_width);
    VP.Height = static_cast<FLOAT>(_height);
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    VP.TopLeftX = 0;
    VP.TopLeftY = 0;
    this->_d3d11_deviceCtx->RSSetViewports(1, &VP);
    //this->_d3d11_deviceCtx->Dispatch(8, 8, 1);
    this->_d3d11_deviceCtx->Dispatch(
                (UINT)ceil(_width * 1.0 / 8),
                (UINT)ceil(_height * 1.0 / 8),
                1);
}

bool DxToFrame::createSharedSurf()
{
    //
    HRESULT hr{ 0 };

    D3D11_TEXTURE2D_DESC texDesc_rgba;
    ZeroMemory(&texDesc_rgba, sizeof(texDesc_rgba));
    texDesc_rgba.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc_rgba.Width = _width;
    texDesc_rgba.Height = _height;
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

    qDebug() << "d3d2dx create texture src";

    //
    D3D11_SHADER_RESOURCE_VIEW_DESC const srcDesc
            = CD3D11_SHADER_RESOURCE_VIEW_DESC(this->_texture_rgba_src.Get(), D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_B8G8R8A8_UNORM);
    hr = this->_d3d11_device->CreateShaderResourceView(this->_texture_rgba_src.Get(), &srcDesc, this->_textureView.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2dx create shader resource view src";

    texDesc_rgba.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_rgba_target.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2dx create texture target";

    // Copy by ndi to av
    texDesc_rgba.BindFlags = 0;
    texDesc_rgba.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_rgba_target_shared.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2dx create texture target shared";

    IDXGIResource* pDXGIResource = NULL;
    _texture_rgba_target_shared->QueryInterface(__uuidof(IDXGIResource), (LPVOID*) &pDXGIResource);
    pDXGIResource->GetSharedHandle(&_texture_rgba_target_shared_handle);
    pDXGIResource->Release();
    if (!_texture_rgba_target_shared_handle){
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

    qDebug() << "d3d2dx create texture copy cpu";

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    hr = this->_d3d11_device->CreateRenderTargetView(this->_texture_rgba_target.Get(), &rtvDesc, this->_renderTargetView.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "d3d2dx create render target view";

    return true;
}

void DxToFrame::releaseSharedSurf()
{
    if (this->_d3d11_deviceCtx.Get() != nullptr)
        this->_d3d11_deviceCtx->ClearState();

    COM_RESET(_textureView);
    COM_RESET(_renderTargetView);

    COM_RESET(_texture_rgba_src);
    COM_RESET(_texture_rgba_target);
    COM_RESET(_texture_rgba_target_shared);
    COM_RESET(_texture_rgba_copy);

    this->_width = 0;
    this->_height = 0;
}

bool DxToFrame::render()
{
    if (!_inited)
        return false;

    if (sources.empty())
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
    for (auto& src : sources) {
        if (!src->copyTo(_d3d11_device.Get(), _d3d11_deviceCtx.Get(), _texture_rgba_src.Get())) {
            continue;
        }
        this->_d3d11_deviceCtx->Draw(NUMVERTICES, 0);
        renderCount++;
    } 
    lock.unlock();
    //e2.end();
    this->_d3d11_deviceCtx->CopyResource(_texture_rgba_target_shared.Get(), _texture_rgba_target.Get());
    this->_d3d11_deviceCtx->Flush();

    renderFps.add(t.nsecsElapsed());

    return renderCount > 0;
}

bool DxToFrame::copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D *dest)
{
    if (!_inited)
        return false;

    lock.lock();

    ID3D11Texture2D *src;
    dev->OpenSharedResource(_texture_rgba_target_shared_handle, __uuidof(ID3D11Texture2D), (LPVOID*) &src);
    if (src == nullptr)
        return false;
     
    D3D11_BOX box = { 0, 0, 0, _width, _height, 1 };
    ctx->CopySubresourceRegion(dest, 0, 0, 0, 0, src, 0, &box);
    ctx->Flush();
    src->Release();

    lock.unlock();

    return true;
}

bool DxToFrame::present() {
    if (!_inited)
        return false;

    if (_swap_chain == nullptr)
        return false;

    this->_d3d11_deviceCtx->CopyResource(_swap_chain_back_buffer.Get(), _texture_rgba_target_shared.Get());
    this->_d3d11_deviceCtx->Flush();

    HRESULT hr = this->_swap_chain->Present(0, 0);
    if (FAILED(hr)) {
        qDebug() << "present failed" << HRESULT_CODE(hr);
        return false;
    }

    return true;
}
