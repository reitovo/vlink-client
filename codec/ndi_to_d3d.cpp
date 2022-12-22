#include "ndi_to_d3d.h"
#include <Processing.NDI.Lib.h>
#include "qdebug.h"
#include "core/util.h"
#include "qtimer.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3d_to_ndi.h>

NdiToDx::NdiToDx(std::shared_ptr<DxToNdi> d3d) : IDxSrc(d3d)
{
    qDebug() << "begin ndi2d3d";
    d3d->registerSource(this);
}

NdiToDx::~NdiToDx()
{
    qDebug() << "end ndi2d3d";
    d3d->unregisterSource(this);

    lock.lock();

    releaseSharedSurf();

    COM_RESET(_d3d11_deviceCtx);
    COM_RESET(_d3d11_device);

    _inited = false;

    lock.unlock();
    qDebug() << "end ndi2d3d done";
}

QString NdiToDx::debugInfo()
{
    return QString("Ndi->Dx (Local Fast Copy) %1").arg(fps.stat());
}

void NdiToDx::update(NDIlib_video_frame_v2_t * frame)
{
    lock.lock();

    D3D11_BOX box = {0, 0, 0, _width, _height, 1};

    //elapsed e("update resource");
    QElapsedTimer t;
    t.start();

    _d3d11_deviceCtx->UpdateSubresource(_texture_rgba.Get(), 0, &box,
                                        frame->p_data, frame->line_stride_in_bytes, 0);
    _d3d11_deviceCtx->Flush();

    fps.add(t.nsecsElapsed());
    //e.end();

    lock.unlock();
}

bool NdiToDx::copyTo(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *dest)
{
    if (!_inited)
        return false;

    lock.lock();

    ID3D11Texture2D *src;
    dev->OpenSharedResource(_texture_rgba_shared, __uuidof(ID3D11Texture2D), (LPVOID*) &src);
    ctx->CopyResource(dest, src);
    src->Release();
    ctx->Flush();

    lock.unlock();

    return true;
}

bool NdiToDx::init()
{
    HRESULT hr;

    qDebug() << "ndi2d3d init";

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

    if (IsDebuggerPresent() && DX_DEBUG_LAYER) {
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    // We need dxgi to share texture
    IDXGIFactory2 *pDXGIFactory;
    IDXGIAdapter *pAdapter = NULL;
    hr = CreateDXGIFactory(IID_IDXGIFactory2, (void **)&pDXGIFactory);
    if (FAILED(hr))
        return false;

    qDebug() << "ndi2d3d create dxgi factory";

    hr = pDXGIFactory->EnumAdapters(0, &pAdapter);
    if (FAILED(hr))
        return false;

    DXGI_ADAPTER_DESC descAdapter;
    hr = pAdapter->GetDesc(&descAdapter);

    qDebug() << "using device" << QString::fromWCharArray(descAdapter.Description);

    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
    {
        hr = D3D11CreateDevice(pAdapter, DriverTypes[DriverTypeIndex], nullptr, creationFlags, FeatureLevels, NumFeatureLevels,
                               D3D11_SDK_VERSION, this->_d3d11_device.GetAddressOf(), &FeatureLevel, this->_d3d11_deviceCtx.GetAddressOf());
        if (SUCCEEDED(hr))
        {
            qDebug() << "ndi2d3d create device successfully";
            // Device creation succeeded, no need to loop anymore
            break;
        }
    }
    if (FAILED(hr))
        return false;

    pDXGIFactory->Release();
    pAdapter->Release();


    auto width = 1920;
    auto height = 1080;
    auto ret = createSharedSurf(width, height);

    _inited = true;

    qDebug() << "ndi2d3d init done";

    return ret;
}

bool NdiToDx::createSharedSurf(int width, int height)
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
    texDesc_rgba.BindFlags = 0;
    texDesc_rgba.Usage = D3D11_USAGE_DEFAULT;
    texDesc_rgba.CPUAccessFlags = 0;
    texDesc_rgba.SampleDesc.Count = 1;
    texDesc_rgba.SampleDesc.Quality = 0;
    texDesc_rgba.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    hr = this->_d3d11_device->CreateTexture2D(&texDesc_rgba, nullptr, this->_texture_rgba.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "ndi2d3d create texture bgra";

    // shared texture
    IDXGIResource* pDXGIResource = NULL;
    _texture_rgba->QueryInterface(__uuidof(IDXGIResource), (LPVOID*) &pDXGIResource);
    pDXGIResource->GetSharedHandle(&_texture_rgba_shared);
    pDXGIResource->Release();
    if (!_texture_rgba_shared){
        return false;
    }

    qDebug() << "ndi2d3d create texture shared handle";

    _width = width;
    _height = height;

    return true;
}

void NdiToDx::releaseSharedSurf()
{
    if (this->_d3d11_deviceCtx.Get() != nullptr)
        this->_d3d11_deviceCtx->ClearState();

    COM_RESET(_texture_rgba);
}
