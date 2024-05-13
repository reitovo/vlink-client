#include "util.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <QString>
#include <dxgi1_2.h>

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <wlanapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wlanapi.lib")

namespace vts::info {
    QString BuildId = "debug";
    bool OverrideForceUseTurn = true;
    QString OverrideTurnServer = "";
}

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

void showTexture(ID3D11DeviceContext *pContext, ID3D11Resource *pSource, QString name) {
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

void setComboBoxIfChanged(const QStringList &strList, QComboBox *box) {
    bool resetList = false;
    if (box->count() != strList.size()) {
        resetList = true;
    } else {
        for (auto i = 0; i < strList.size(); ++i) {
            if (strList[i] != box->itemText(i)){
                resetList = true;
                break;
            }
        }
    }
    if (resetList) {
        box->clear();
        box->addItems(strList);
    }
}



void printDxLiveObjects(IUnknown *dev, const char * func) {
    if (IsDebuggerPresent() && DX_DEBUG_LAYER) {
        ID3D11Debug *d3dDebug;
        HRESULT hr = dev->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void **>(&d3dDebug));
        if (SUCCEEDED(hr)) {
            qDebug() << "Report Live Objects of" << func;
            hr = d3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
        }
        if (d3dDebug != nullptr)
            d3dDebug->Release();
    }
}

void setDxDebugName(ID3D11DeviceChild *child, const std::string &name) {
    if (child != nullptr)
        child->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());
}

bool isElevated() {
    BOOL fIsElevated = FALSE;
    HANDLE hToken = NULL;
    TOKEN_ELEVATION elevation;
    DWORD dwSize;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        printf("\n Failed to get Process Token :%d.",GetLastError());
        goto Cleanup;  // if Failed, we treat as False
    }


    if (!GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize))
    {
        printf("\nFailed to get Token Information :%d.", GetLastError());
        goto Cleanup;// if Failed, we treat as False
    }

    fIsElevated = elevation.TokenIsElevated;

    Cleanup:
    if (hToken)
    {
        CloseHandle(hToken);
        hToken = NULL;
    }
    return fIsElevated;
}

std::string getPrimaryGpu() {
    // We need dxgi to share texture
    IDXGIFactory2 *pDXGIFactory;
    IDXGIAdapter *pAdapter = NULL;
    HRESULT hr = CreateDXGIFactory(IID_IDXGIFactory2, (void **) &pDXGIFactory);
    if (FAILED(hr)) {
        qDebug() << "failed to create dxgi factory";
        return "Failed To Get GPU";
    }

    hr = pDXGIFactory->EnumAdapters(0, &pAdapter);
    if (FAILED(hr)) {
        return "Failed To Get GPU";
    }

    DXGI_ADAPTER_DESC descAdapter;
    hr = pAdapter->GetDesc(&descAdapter);
    if (FAILED(hr)) {
        return "Failed To Get GPU";
    }

    auto name = QString::fromWCharArray(descAdapter.Description);
    auto vendor = getGpuVendorTypeFromVendorId(descAdapter.VendorId);
    return getGpuVendorName(vendor).toStdString() + " (" + name.toStdString() + ")";
}

bool is2G4Wireless() {
    HANDLE hClient;
    DWORD dwCurVersion;
    WLAN_INTERFACE_INFO_LIST* pInterfaceList;

    // 初始化 WlanApi
    if (WlanOpenHandle(2, NULL, &dwCurVersion, &hClient) != ERROR_SUCCESS) {
        std::cerr << "WlanOpenHandle failed." << std::endl;
        return false;
    }

    // 获取无线适配器列表信息
    if (WlanEnumInterfaces(hClient, NULL, &pInterfaceList) != ERROR_SUCCESS) {
        std::cerr << "WlanEnumInterfaces failed." << std::endl;
        WlanCloseHandle(hClient, NULL);
        return false;
    }

    auto has24Channel = false;
    // 逐个处理找到的无线适配器
    for (unsigned i = 0; i < pInterfaceList->dwNumberOfItems; ++i) {
        WLAN_INTERFACE_INFO* pInterfaceInfo = &pInterfaceList->InterfaceInfo[i];

        ULONG *channel = NULL;
        DWORD dwSizeChannel = sizeof(*channel);

        DWORD rc = WlanQueryInterface (
                hClient, &pInterfaceInfo->InterfaceGuid,
                wlan_intf_opcode_channel_number,
                NULL, &dwSizeChannel, reinterpret_cast<PVOID *>(&channel), NULL);

        if (rc == ERROR_SUCCESS && channel) {
            std::cout << "Channel: " << *channel << std::endl;
            if (*channel <= 14) {
                has24Channel = true;
            }
            WlanFreeMemory (channel);
        }
    }

    // 释放内存并关闭 WlanApi
    WlanFreeMemory(pInterfaceList);
    WlanCloseHandle(hClient, NULL);

    return has24Channel;
}

bool isWireless() {
    ULONG bufferSize = 0;
    IP_ADAPTER_ADDRESSES* adapters = nullptr;

    // 获取适配器信息的缓冲区大小
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufferSize);
    assert(result == ERROR_BUFFER_OVERFLOW);

    // 分配缓冲区并重新获取适配器信息
    adapters = static_cast<IP_ADAPTER_ADDRESSES*>(malloc(bufferSize));
    result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &bufferSize);
    assert(result == NO_ERROR);

    auto hasUpWireless = false;

    // 遍历适配器列表
    IP_ADAPTER_ADDRESSES* adapter = adapters;
    while (adapter) {
        std::wcout << L"适配器名称: " << adapter->AdapterName << std::endl;
        std::wcout << L"适配器描述: " << adapter->Description << std::endl;

        std::wcout << L"适配器类型: ";
        if (adapter->IfType == IF_TYPE_IEEE80211) {
            std::wcout << L"无线(WiFi)" << std::endl;
        } else if (adapter->IfType == IF_TYPE_ETHERNET_CSMACD) {
            std::wcout << L"有线(Ethernet)" << std::endl;
        } else {
            std::wcout << L"其他类型(" << adapter->IfType << L")" << std::endl;
        }

        std::wcout << L"适配器状态: ";
        if (adapter->OperStatus == IfOperStatusUp) {
            std::wcout << L"已连接" << std::endl;
        } else if (adapter->OperStatus == IfOperStatusDown) {
            std::wcout << L"未连接" << std::endl;
        } else {
            std::wcout << L"其他类型(" << adapter->OperStatus << L")" << std::endl;
        }

        if (adapter->OperStatus == IfOperStatusUp && adapter->IfType == IF_TYPE_IEEE80211) {
            std::wcout << L"找到了活动的无线网络连接" << std::endl;
            hasUpWireless = true;
        }

        adapter = adapter->Next;
        std::wcout << std::endl;
    }

    // 释放内存并退出
    free(adapters);
    return hasUpWireless;
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
