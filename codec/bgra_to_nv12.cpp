#include "bgra_to_nv12.h"
#include "core/util.h"
#include "d3d_to_frame.h"
#include <d3d11.h>
#include <dxcore.h>
#include <DirectXMath.h>
#include <QDebug>
#include <QFile>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <mfx/mfxcommon.h>
#include <mfx/mfxstructures.h>
#include <directxtk/ScreenGrab.h>

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

BgraToNv12::BgraToNv12(int width, int height)
{
	qDebug() << "begin bgranv12";
    _width = width;
    _height = height;
}

BgraToNv12::~BgraToNv12() {
	qDebug() << "end bgranv12";

	lock.lock();

	releaseSharedSurf();

	COM_RESET(_d3d11_vertexBuffer);

	COM_RESET(_d3d11_nv24_nv12_ps);
	COM_RESET(_d3d11_nv24_nv12_vs);
	COM_RESET(_d3d11_bgra_nv24_ps);
	COM_RESET(_d3d11_bgra_nv24_vs);

	COM_RESET(_d3d11_inputLayout);
	COM_RESET(_d3d11_samplerState);

	COM_RESET(_bgra_nv24_vertex_shader);
	COM_RESET(_bgra_nv24_pixel_shader);
	COM_RESET(_nv24_nv12_vertex_shader);
	COM_RESET(_nv24_nv12_pixel_shader);

	COM_RESET(_d3d11_deviceCtx);
	COM_RESET(_d3d11_device);

	lock.unlock();
	qDebug() << "end bgranv12 done";
}

bool BgraToNv12::compileShader()
{
	QFile f1(":/shader/bgranv12_vertex.hlsl");
	f1.open(QIODevice::ReadOnly);
	auto s = QString(f1.readAll()).toStdString();
	HRESULT hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
		"VS", "vs_5_0", 0, 0, _bgra_nv24_vertex_shader.GetAddressOf(), nullptr);
	if (FAILED(hr)) {
		qCritical() << "failed compiling bgranv12 vertex shader";
		return false;
	}

	QFile f2(":/shader/bgranv12_pixel.hlsl");
	f2.open(QIODevice::ReadOnly);
	s = QString(f2.readAll()).toStdString();
	hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
		"PS", "ps_5_0", 0, 0, _bgra_nv24_pixel_shader.GetAddressOf(), nullptr);
	if (FAILED(hr)) {
		qCritical() << "failed compiling bgranv12 pixel shader";
		return false;
	}

	QFile f3(":/shader/bgranv12_downsample_vertex.hlsl");
	f3.open(QIODevice::ReadOnly);
	s = QString(f3.readAll()).toStdString();
	hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
		"VS", "vs_5_0", 0, 0, _nv24_nv12_vertex_shader.GetAddressOf(), nullptr);
	if (FAILED(hr)) {
		qCritical() << "failed compiling bgranv12 downsample vertex shader";
		return false;
	}

	QFile f4(":/shader/bgranv12_downsample_pixel.hlsl");
	f4.open(QIODevice::ReadOnly);
	s = QString(f4.readAll()).toStdString();
	hr = D3DCompile(s.c_str(), s.size(), nullptr, nullptr, nullptr,
		"PS", "ps_5_0", 0, 0, _nv24_nv12_pixel_shader.GetAddressOf(), nullptr);
	if (FAILED(hr)) {
		qCritical() << "failed compiling bgranv12 downsample pixel shader";
		return false;
	}

	qDebug() << "bgranv12 compile shader done";
	return true;
}

void BgraToNv12::releaseSharedSurf()
{
	if (this->_d3d11_deviceCtx.Get() != nullptr)
		this->_d3d11_deviceCtx->ClearState();

	COM_RESET(_bgraView);
	COM_RESET(_downsampleView);

	COM_RESET(_rtv_nv12_y);
	COM_RESET(_rtv_nv12_uv);
	COM_RESET(_rtv_bgra_uv);

	COM_RESET(_texture_bgra);
	COM_RESET(_texture_uv_target);
    COM_RESET(_texture_uv_target_copy);
	COM_RESET(_texture_nv12_target);
	COM_RESET(_texture_nv12_copy_target);

	this->_width = 0;
	this->_height = 0;
}

ID3D11Device* BgraToNv12::getDevice() {
	_d3d11_device->AddRef();
	return _d3d11_device.Get();
}

DeviceAdapterType BgraToNv12::getDeviceVendor()
{
	return _vendor;
}

bool BgraToNv12::init()
{
	HRESULT hr;

	qDebug() << "bgranv12 init";

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
	IDXGIFactory2* pDXGIFactory;
	IDXGIAdapter* pAdapter = NULL;
	hr = CreateDXGIFactory(IID_IDXGIFactory2, (void**)&pDXGIFactory);
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create dxgi factory";

	hr = pDXGIFactory->EnumAdapters(0, &pAdapter);
	if (FAILED(hr))
		return false;

	DXGI_ADAPTER_DESC descAdapter;
	hr = pAdapter->GetDesc(&descAdapter);
	qDebug() << "using device" << QString::fromWCharArray(descAdapter.Description);
	_vendor = getGpuVendorTypeFromVendorId(descAdapter.VendorId);
	qDebug() << "device vendor" << getGpuVendorName(_vendor);
	
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(pAdapter, DriverTypes[DriverTypeIndex], nullptr, creationFlags, FeatureLevels, NumFeatureLevels,
			D3D11_SDK_VERSION, this->_d3d11_device.GetAddressOf(), &FeatureLevel, this->_d3d11_deviceCtx.GetAddressOf());
		if (SUCCEEDED(hr))
		{
			qDebug() << "bgranv12 d3d11 create device success";
			// Device creation succeeded, no need to loop anymore
			break;
		}
	}
	if (FAILED(hr)) {
		qDebug() << "bgranv12 d3d11 create device failed" << HRESULT_CODE(hr);
		return false;
	}

	pDXGIFactory->Release();
	pAdapter->Release();

	ComPtr<ID3D10Multithread> pD10Multithread;
	_d3d11_deviceCtx->QueryInterface(IID_ID3D10Multithread, (void**)pD10Multithread.GetAddressOf());
	pD10Multithread->SetMultithreadProtected(true);

	//SamplerState
	D3D11_SAMPLER_DESC desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
	hr = this->_d3d11_device->CreateSamplerState(&desc, this->_d3d11_samplerState.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create sampler state";

	//VertexShader
	hr = this->_d3d11_device->CreateVertexShader(_bgra_nv24_vertex_shader->GetBufferPointer(), _bgra_nv24_vertex_shader->GetBufferSize(),
		nullptr, this->_d3d11_bgra_nv24_vs.GetAddressOf());
	if (FAILED(hr))
		return false;

	hr = this->_d3d11_device->CreateVertexShader(_nv24_nv12_vertex_shader->GetBufferPointer(), _nv24_nv12_vertex_shader->GetBufferSize(),
		nullptr, this->_d3d11_nv24_nv12_vs.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create vertex shader";

	constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> Layout =
	{ {
			// 3D 32bit float vector
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			// 2D 32bit float vector
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		} };

	//Once an input-layout object is created from a shader signature, the input-layout object
	//can be reused with any other shader that has an identical input signature (semantics included).
	//This can simplify the creation of input-layout objects when you are working with many shaders with identical inputs.
	//https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout
	hr = this->_d3d11_device->CreateInputLayout(Layout.data(), Layout.size(),
		_bgra_nv24_vertex_shader->GetBufferPointer(), _bgra_nv24_vertex_shader->GetBufferSize(),
		this->_d3d11_inputLayout.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create input layout";

	//PixelShader
	hr = this->_d3d11_device->CreatePixelShader(_bgra_nv24_pixel_shader->GetBufferPointer(), _bgra_nv24_pixel_shader->GetBufferSize(), nullptr, this->_d3d11_bgra_nv24_ps.GetAddressOf());
	if (FAILED(hr))
		return false;

	hr = this->_d3d11_device->CreatePixelShader(_nv24_nv12_pixel_shader->GetBufferPointer(), _nv24_nv12_pixel_shader->GetBufferSize(), nullptr, this->_d3d11_nv24_nv12_ps.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create pixel shader";

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

	qDebug() << "bgranv12 create vertex buffer";

	auto ret = createSharedSurf();
	initDeviceContext();
	initViewport();

	_inited = true;

	qDebug() << "bgranv12 init done";
	return ret;
}

void BgraToNv12::initDeviceContext()
{
	//init
	this->_d3d11_deviceCtx->IASetInputLayout(this->_d3d11_inputLayout.Get());
	this->_d3d11_deviceCtx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
	this->_d3d11_deviceCtx->PSSetSamplers(0, 1, this->_d3d11_samplerState.GetAddressOf());
	this->_d3d11_deviceCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	this->_d3d11_deviceCtx->IASetVertexBuffers(0, 1, this->_d3d11_vertexBuffer.GetAddressOf(), &Stride, &Offset);

	qDebug() << "bgranv12 set context params";

	//SharedSurf
	_bgra_nv24_sr = {
		this->_bgraView.Get(),
	};

	_bgra_nv24_rt = {
		this->_rtv_nv12_y.Get(),
		this->_rtv_bgra_uv.Get()
	};

	//SharedSurf
	_nv24_nv12_sr = {
		this->_downsampleView.Get(),
	};

	_nv24_nv12_rt = {
		this->_rtv_nv12_uv.Get()
	};

	qDebug() << "bgranv12 set context textures";

	//this->_d3d11_deviceCtx->Dispatch(8, 8, 1);
	this->_d3d11_deviceCtx->Dispatch(
		(UINT)ceil(_width * 1.0 / 8),
		(UINT)ceil(_height * 2 * 1.0 / 8),
		1);

	qDebug() << "bgranv12 set dispatch";
}

void BgraToNv12::setBgraToNv24Context()
{
	this->_d3d11_deviceCtx->OMSetRenderTargets(_bgra_nv24_rt.size(), _bgra_nv24_rt.data(), nullptr);
	this->_d3d11_deviceCtx->PSSetShaderResources(0, _bgra_nv24_sr.size(), _bgra_nv24_sr.data());
	this->_d3d11_deviceCtx->VSSetShader(this->_d3d11_bgra_nv24_vs.Get(), nullptr, 0);
	this->_d3d11_deviceCtx->PSSetShader(this->_d3d11_bgra_nv24_ps.Get(), nullptr, 0);
}

void BgraToNv12::setNv24ToNv12Context()
{
	this->_d3d11_deviceCtx->OMSetRenderTargets(_nv24_nv12_rt.size(), _nv24_nv12_rt.data(), nullptr);
	this->_d3d11_deviceCtx->PSSetShaderResources(0, _nv24_nv12_sr.size(), _nv24_nv12_sr.data());
	this->_d3d11_deviceCtx->VSSetShader(this->_d3d11_nv24_nv12_vs.Get(), nullptr, 0);
	this->_d3d11_deviceCtx->PSSetShader(this->_d3d11_nv24_nv12_ps.Get(), nullptr, 0);
}

void BgraToNv12::initViewport()
{
	D3D11_VIEWPORT VP;
	VP.Width = static_cast<FLOAT>(_width);
	VP.Height = static_cast<FLOAT>(_height * 2);
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	this->_d3d11_deviceCtx->RSSetViewports(1, &VP);

	qDebug() << "bgranv12 set context viewport";
}

bool BgraToNv12::createSharedSurf()
{
	//
	HRESULT hr{ 0 };

	//Create input texture
	D3D11_TEXTURE2D_DESC texDesc_input_bgra;
	ZeroMemory(&texDesc_input_bgra, sizeof(texDesc_input_bgra));
	texDesc_input_bgra.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc_input_bgra.Width = _width;
	texDesc_input_bgra.Height = _height;
	texDesc_input_bgra.ArraySize = 1;
	texDesc_input_bgra.MipLevels = 1;
	texDesc_input_bgra.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc_input_bgra.Usage = D3D11_USAGE_DEFAULT;
	texDesc_input_bgra.CPUAccessFlags = 0;
	texDesc_input_bgra.SampleDesc.Count = 1;
	texDesc_input_bgra.SampleDesc.Quality = 0;
	texDesc_input_bgra.MiscFlags = 0;

	hr = this->_d3d11_device->CreateTexture2D(&texDesc_input_bgra, nullptr, this->_texture_bgra.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create texture bgra input";

	std::string name = "bgranv12 bgra input";
	_texture_bgra->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

	//
	D3D11_SHADER_RESOURCE_VIEW_DESC const bgraPlaneDesc
		= CD3D11_SHADER_RESOURCE_VIEW_DESC(this->_texture_bgra.Get(), D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_B8G8R8A8_UNORM);
	hr = this->_d3d11_device->CreateShaderResourceView(this->_texture_bgra.Get(), &bgraPlaneDesc, this->_bgraView.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create shader resview bgra";

	//Create output texture
	D3D11_TEXTURE2D_DESC texDesc_nv12;
	ZeroMemory(&texDesc_nv12, sizeof(texDesc_nv12));
	texDesc_nv12.Format = DXGI_FORMAT_NV12;
	texDesc_nv12.Width = _width;
	texDesc_nv12.Height = _height * 2;
	texDesc_nv12.ArraySize = 1;
	texDesc_nv12.MipLevels = 1;
	texDesc_nv12.BindFlags = D3D11_BIND_RENDER_TARGET;
	texDesc_nv12.Usage = D3D11_USAGE_DEFAULT;
	texDesc_nv12.CPUAccessFlags = 0;
	texDesc_nv12.SampleDesc.Count = 1;
	texDesc_nv12.SampleDesc.Quality = 0;
	texDesc_nv12.MiscFlags = 0;

	hr = this->_d3d11_device->CreateTexture2D(&texDesc_nv12, nullptr, this->_texture_nv12_target.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create texture nv12";

	name = "bgranv12 nv12";
    _texture_nv12_target->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

	texDesc_nv12.BindFlags = 0;
	texDesc_nv12.Usage = D3D11_USAGE_STAGING;
	texDesc_nv12.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	hr = this->_d3d11_device->CreateTexture2D(&texDesc_nv12, nullptr, this->_texture_nv12_copy_target.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create texture nv12 rgb copy";

	name = "bgranv12 nv12 rgb copy";
    _texture_nv12_copy_target->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

	// create a full size uv plane for rgb
	D3D11_TEXTURE2D_DESC texDesc_uv;
	ZeroMemory(&texDesc_uv, sizeof(texDesc_uv));
	texDesc_uv.Format = DXGI_FORMAT_R8G8_UNORM;
	texDesc_uv.Width = _width;
	texDesc_uv.Height = _height * 2;
	texDesc_uv.ArraySize = 1;
	texDesc_uv.MipLevels = 1;
	texDesc_uv.BindFlags = D3D11_BIND_RENDER_TARGET;
	texDesc_uv.Usage = D3D11_USAGE_DEFAULT;
	texDesc_uv.CPUAccessFlags = 0;
	texDesc_uv.SampleDesc.Count = 1;
	texDesc_uv.SampleDesc.Quality = 0;
	texDesc_uv.MiscFlags = 0;
	hr = this->_d3d11_device->CreateTexture2D(&texDesc_uv, nullptr, this->_texture_uv_target.GetAddressOf());
	if (FAILED(hr))
		return false;

	qDebug() << "bgranv12 create texture bgra uv";

	name = "bgranv12 bgra uv";
	_texture_uv_target->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

    texDesc_uv.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = this->_d3d11_device->CreateTexture2D(&texDesc_uv, nullptr, this->_texture_uv_target_copy.GetAddressOf());
    if (FAILED(hr))
        return false;

    qDebug() << "bgranv12 create texture bgra uv copy";

    name = "bgranv12 bgra uv copy";
    _texture_uv_target_copy->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

	D3D11_SHADER_RESOURCE_VIEW_DESC const downsamplePlaneDesc
		= CD3D11_SHADER_RESOURCE_VIEW_DESC(this->_texture_uv_target_copy.Get(), D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8_UNORM);
	hr = this->_d3d11_device->CreateShaderResourceView(this->_texture_uv_target_copy.Get(), &downsamplePlaneDesc, this->_downsampleView.GetAddressOf());
	if (FAILED(hr))
		return false;

	// create render target views
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	hr = this->_d3d11_device->CreateRenderTargetView(this->_texture_nv12_target.Get(), &rtvDesc, this->_rtv_nv12_y.GetAddressOf());
	if (FAILED(hr))
		return false;
	qDebug() << "bgranv12 create nv12 y rtv";

	rtvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
	hr = this->_d3d11_device->CreateRenderTargetView(this->_texture_nv12_target.Get(), &rtvDesc, this->_rtv_nv12_uv.GetAddressOf());
	if (FAILED(hr))
		return false;
	qDebug() << "bgranv12 create nv12 uv rtv";

	// create a full size uv rtv for rgb
	hr = this->_d3d11_device->CreateRenderTargetView(this->_texture_uv_target.Get(), &rtvDesc, this->_rtv_bgra_uv.GetAddressOf());
	if (FAILED(hr))
		return false;
	qDebug() << "bgranv12 create bgra uv rtv";

	// Set alpha stream's uv to 128, as it is a grayscale alpha channel stream
	FLOAT a_uv_color[4] = { 0.501960, 0.501960, 0.501960, 1 };
	this->_d3d11_deviceCtx->ClearRenderTargetView(_rtv_nv12_uv.Get(), a_uv_color);

	qDebug() << "bgranv12 create render target views";

	return true;
}

bool BgraToNv12::bgraToNv12Fast(const std::shared_ptr<IDxCopyable>& fast)
{
    HRESULT hr{ 0 };

    if (!_inited) {
        return false;
    }

    //Elapsed e1("update subresource"); //~300000 ns
    if (fast != nullptr) {
        fast->copyTo(_d3d11_device.Get(), _d3d11_deviceCtx.Get(), _texture_bgra.Get());
    } else {
        return false;
    }
    //e1.end();

    //Elapsed e2("draw 1"); //~17500 ns
    setBgraToNv24Context();
    this->_d3d11_deviceCtx->Draw(NUMVERTICES, 0);
    //e2.end();

    this->_d3d11_deviceCtx->CopyResource(_texture_uv_target_copy.Get(), _texture_uv_target.Get());

    //Elapsed e4("draw 2"); //~11100 ns
    setNv24ToNv12Context();
    this->_d3d11_deviceCtx->Draw(NUMVERTICES, 0);

    this->_d3d11_deviceCtx->Flush();
    //e4.end();

    return true;
}

void BgraToNv12::copyFrameD3D11(AVFrame* f)
{
	if (!_inited)
		return;

    // nv12 tex, 2x height
	D3D11_BOX srcBox = { 0, 0, 0, _width, _height * 2, 1 };
	this->_d3d11_deviceCtx->CopySubresourceRegion((ID3D11Resource*)f->data[0], (int)f->data[1], 0, 0, 0,
		_texture_nv12_target.Get(), 0, &srcBox);
}

static void getD3D11TextureFromQSV(AVFrame* frame, ID3D11Resource** tex, int* id) {
	mfxFrameSurface1* surface = (mfxFrameSurface1*)frame->data[3];
	mfxHDLPair* pair = (mfxHDLPair*)surface->Data.MemId;
	*tex = (ID3D11Resource*)pair->first;
	*id = pair->second == (mfxMemId)MFX_INFINITE ? 0 : (int)pair->second;
}

void BgraToNv12::copyFrameQSV(AVFrame* f)
{
	if (!_inited)
		return;

    // nv12 tex, 2x height
	D3D11_BOX srcBox = { 0, 0, 0, _width, _height * 2, 1 };
	ID3D11Resource* tex;
	int id;

	getD3D11TextureFromQSV(f, &tex, &id);
	this->_d3d11_deviceCtx->CopySubresourceRegion(tex, id, 0, 0, 0,
		_texture_nv12_target.Get(), 0, &srcBox);
}