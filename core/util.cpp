#include "util.h"
#include <d3d11.h>
#include <wrl/client.h>
#include "libyuv.h"

#ifdef HAS_DIRECTXTK
#include "DirectXHelpers.h"
#endif

#ifdef HAS_OPENCV
#include "opencv2/opencv.hpp"
#endif

using Microsoft::WRL::ComPtr;

#ifdef HAS_DIRECTXTK
namespace
{
    //--------------------------------------------------------------------------------------
    HRESULT CaptureTexture(
            _In_ ID3D11DeviceContext* pContext,
            _In_ ID3D11Resource* pSource,
            D3D11_TEXTURE2D_DESC& desc,
            ComPtr<ID3D11Texture2D>& pStaging) noexcept
    {
        if (!pContext || !pSource)
            return E_INVALIDARG;

        D3D11_RESOURCE_DIMENSION resType = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        pSource->GetType(&resType);

        if (resType != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }

        ComPtr<ID3D11Texture2D> pTexture;
        HRESULT hr = pSource->QueryInterface(IID_GRAPHICS_PPV_ARGS(pTexture.GetAddressOf()));
        if (FAILED(hr))
            return hr;

        assert(pTexture);

        pTexture->GetDesc(&desc);


        ComPtr<ID3D11Device> d3dDevice;
        pContext->GetDevice(d3dDevice.GetAddressOf());

        if (desc.SampleDesc.Count > 1)
        {
            // MSAA content must be resolved before being copied to a staging texture
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;

            ComPtr<ID3D11Texture2D> pTemp;
            hr = d3dDevice->CreateTexture2D(&desc, nullptr, pTemp.GetAddressOf());
            if (FAILED(hr))
                return hr;

            assert(pTemp);

            const DXGI_FORMAT fmt = desc.Format;

            UINT support = 0;
            hr = d3dDevice->CheckFormatSupport(fmt, &support);
            if (FAILED(hr))
                return hr;

            if (!(support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE))
                return E_FAIL;

            for (UINT item = 0; item < desc.ArraySize; ++item)
            {
                for (UINT level = 0; level < desc.MipLevels; ++level)
                {
                    const UINT index = D3D11CalcSubresource(level, item, desc.MipLevels);
                    pContext->ResolveSubresource(pTemp.Get(), index, pSource, index, fmt);
                }
            }

            desc.BindFlags = 0;
            desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.Usage = D3D11_USAGE_STAGING;

            hr = d3dDevice->CreateTexture2D(&desc, nullptr, pStaging.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                return hr;

            assert(pStaging);

            pContext->CopyResource(pStaging.Get(), pTemp.Get());
        }
        else if ((desc.Usage == D3D11_USAGE_STAGING) && (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ))
        {
            // Handle case where the source is already a staging texture we can use directly
            pStaging = pTexture;
        }
        else
        {
            // Otherwise, create a staging texture from the non-MSAA source
            desc.BindFlags = 0;
            desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.Usage = D3D11_USAGE_STAGING;

            hr = d3dDevice->CreateTexture2D(&desc, nullptr, pStaging.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                return hr;

            assert(pStaging);

            pContext->CopyResource(pStaging.Get(), pSource);
        }

        return S_OK;
    }
} // anonymous namespace
#endif

Elapsed::Elapsed(const QString &name)
{
    this->name = name;
    timer.start();
}

Elapsed::~Elapsed()
{
    if (!ended)
        end();
}

void Elapsed::end()
{
    auto e = timer.nsecsElapsed();
    qDebug() << name << "cost" << e << "ns";
    ended = true;
}

void printDxDebugInfo(ID3D11Device *dev)
{
    ID3D11InfoQueue* debug_info_queue;
    dev->QueryInterface(__uuidof(ID3D11InfoQueue), (void **)&debug_info_queue);

    auto hr = debug_info_queue->PushEmptyStorageFilter();

    UINT64 message_count = debug_info_queue->GetNumStoredMessages();

    for(UINT64 i = 0; i < message_count; i++){
        SIZE_T message_size = 0;
        debug_info_queue->GetMessage(i, nullptr, &message_size); //get the size of the message

        D3D11_MESSAGE* message = (D3D11_MESSAGE*) malloc(message_size); //allocate enough space
        hr = debug_info_queue->GetMessage(i, message, &message_size); //get the actual message

        //do whatever you want to do with it
        qDebug() << "Direct3D 11:" << QString::fromStdString(std::string(message->pDescription, message->DescriptionByteLength));

        free(message);
    }

    debug_info_queue->ClearStoredMessages();
}

void saveTextureToFile(ID3D11DeviceContext *pContext, ID3D11Resource *pSource, QString name) {
#ifdef HAS_OPENCV
    D3D11_TEXTURE2D_DESC desc = {};
    ComPtr<ID3D11Texture2D> pStaging;
    HRESULT hr = CaptureTexture(pContext, pSource, desc, pStaging);
    if (FAILED(hr))
        return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = pContext->Map(pStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return;

    auto bgra = std::make_unique<uint8_t[]>(desc.Width * desc.Height * 4);

    if (desc.Format == DXGI_FORMAT_NV12) {
        libyuv::NV12ToARGB((uint8_t*)mapped.pData, mapped.RowPitch,
                           (uint8_t*)mapped.pData + mapped.RowPitch * desc.Height, mapped.RowPitch,
                           bgra.get(), desc.Width * 4, desc.Width, desc.Height);
    } else if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        libyuv::ARGBCopy((uint8_t*)mapped.pData, mapped.RowPitch, bgra.get(), desc.Width*4, desc.Width, desc.Height);
    }

    pContext->Unmap(pStaging.Get(), 0);

    cv::Mat mat(desc.Height, desc.Width, CV_8UC4, bgra.get());
    cv::imshow(name.toStdString(), mat);
    cv::waitKey(1);
#endif
}

void FpsCounter::add(long nsConsumed)
{
    auto sec = QDateTime::currentSecsSinceEpoch();
    if (sec != currentSec) {
        currentSec = sec;
        lastCount = count;
        count = 0;
    }
    count++;
    nsAverage = (nsAverage * 31 + nsConsumed) / 32;
    lastAddSec = sec;
}

QString FpsCounter::stat()
{
    auto sec = QDateTime::currentSecsSinceEpoch();
    if (sec - lastAddSec > 3) {
        return QString("FPS: - Cost: - us");
    }
    return QString("FPS: %2 Cost: %3 us").arg(lastCount).arg(nsAverage / 1000);
}

double FpsCounter::fps()
{
    auto sec = QDateTime::currentSecsSinceEpoch();
    if (sec - lastAddSec > 3) {
        return 0.0;
    }

    return 1000000000.0 / nsAverage;
}

long FpsCounter::ns()
{
    return nsAverage;
}
