// D3D11 for the frame cases: a device on the first adapter (WARP without one), textures made with the pattern as
// their initial data (multisampled ones cleared quadrant by quadrant).

#include "GfxApi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11_1.h>
#include <dxgi1_2.h>

gfx::Texels TexelsOfDxgi(int64_t format)
{
	switch (format) {
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return gfx::Texels::RGBA8;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return gfx::Texels::BGRA8;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return gfx::Texels::RGBA16F;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
		return gfx::Texels::RGB10A2;
	default:
		return gfx::Texels::Unknown;
	}
}

namespace {

// The typed format a view of a typeless one uses
DXGI_FORMAT ViewFormat(DXGI_FORMAT f)
{
	switch (f) {
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		return DXGI_FORMAT_R10G10B10A2_UNORM;
	default:
		return f;
	}
}

class D3D11Gfx : public GfxApi {
public:
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	std::vector<ID3D11Texture2D*> made;

	~D3D11Gfx() override
	{
		for (ID3D11Texture2D* t : made)
			t->Release();
		if (context)
			context->Release();
		if (device)
			device->Release();
	}

	GfxTexture Wrap(ID3D11Texture2D* t)
	{
		GfxTexture g;
		g.tex.eType = vr::TextureType_DirectX;
		g.tex.eColorSpace = vr::ColorSpace_Auto;
		g.tex.handle = t;
		g.keep = t;
		if (t)
			made.push_back(t);
		return g;
	}

	GfxTexture Pattern(uint32_t w, uint32_t h, int64_t nativeFormat, int eye, uint32_t samples, uint32_t arraySize) override
	{
		gfx::Texels texels = TexelsOfDxgi(nativeFormat);
		if (texels == gfx::Texels::Unknown || !w || !h)
			return Wrap(nullptr);
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = 1;
		desc.ArraySize = arraySize ? arraySize : 1;
		desc.Format = (DXGI_FORMAT)nativeFormat;
		desc.SampleDesc.Count = samples ? samples : 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		ID3D11Texture2D* t = nullptr;
		if (desc.SampleDesc.Count == 1) {
			size_t pitch = (size_t)w * gfx::BytesOf(texels);
			std::vector<uint8_t> data = gfx::PatternTexels(texels, w, h, eye, pitch, false);
			std::vector<D3D11_SUBRESOURCE_DATA> init(desc.ArraySize, { data.data(), (UINT)pitch, 0 });
			if (FAILED(device->CreateTexture2D(&desc, init.data(), &t)))
				return Wrap(nullptr);
			return Wrap(t);
		}
		// Multisampled: no initial data; each quadrant cleared through a render target view (D3D11.1's ClearView)
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &t)))
			return Wrap(nullptr);
		ID3D11DeviceContext1* ctx1 = nullptr;
		context->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&ctx1);
		D3D11_RENDER_TARGET_VIEW_DESC rd{};
		rd.Format = ViewFormat(desc.Format);
		rd.ViewDimension = desc.ArraySize > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMS;
		rd.Texture2DMSArray.ArraySize = desc.ArraySize;
		ID3D11RenderTargetView* rtv = nullptr;
		if (!ctx1 || FAILED(device->CreateRenderTargetView(t, &rd, &rtv))) {
			if (ctx1)
				ctx1->Release();
			t->Release();
			return Wrap(nullptr);
		}
		uint32_t hw = w / 2, hh = h / 2;
		const D3D11_RECT rects[4] = { { 0, 0, (LONG)hw, (LONG)hh }, { (LONG)hw, 0, (LONG)w, (LONG)hh },
			{ 0, (LONG)hh, (LONG)hw, (LONG)h }, { (LONG)hw, (LONG)hh, (LONG)w, (LONG)h } };
		for (const D3D11_RECT& r : rects) {
			uint32_t c = gfx::PatternColour((uint32_t)r.left, (uint32_t)r.top, w, h, eye);
			const float colour[4] = { (c >> 24) / 255.0f, ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f };
			ctx1->ClearView(rtv, colour, &r, 1);
		}
		rtv->Release();
		ctx1->Release();
		return Wrap(t);
	}

	GfxTexture Depth(uint32_t w, uint32_t h, float value) override
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
		std::vector<float> data((size_t)w * h, value);
		D3D11_SUBRESOURCE_DATA init{ data.data(), w * 4, 0 };
		ID3D11Texture2D* t = nullptr;
		if (SUCCEEDED(device->CreateTexture2D(&desc, &init, &t)))
			return Wrap(t);
		// Without initial data, cleared through a depth view
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &t)))
			return Wrap(nullptr);
		D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
		dd.Format = DXGI_FORMAT_D32_FLOAT;
		dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		ID3D11DepthStencilView* dsv = nullptr;
		if (SUCCEEDED(device->CreateDepthStencilView(t, &dd, &dsv))) {
			context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, value, 0);
			dsv->Release();
		}
		return Wrap(t);
	}

	void Flush() override
	{
		context->Flush();
		// A query that's done once everything before it is
		D3D11_QUERY_DESC qd{ D3D11_QUERY_EVENT, 0 };
		ID3D11Query* q = nullptr;
		if (SUCCEEDED(device->CreateQuery(&qd, &q))) {
			context->End(q);
			BOOL done = FALSE;
			for (int i = 0; i < 5000 && context->GetData(q, &done, sizeof(done), 0) != S_OK; i++)
				Sleep(1); // at most about 5 s
			q->Release();
		}
	}
};

} // namespace

std::unique_ptr<GfxApi> MakeD3D11Gfx(std::string* whyNot)
{
	auto g = std::make_unique<D3D11Gfx>();
	const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	// The first adapter, which the stand-in (as Monado) names in its requirements and checks the device against
	HRESULT hr = E_FAIL;
	IDXGIFactory1* factory = nullptr;
	IDXGIAdapter1* adapter = nullptr;
	if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) && factory->EnumAdapters1(0, &adapter) == S_OK) {
		hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &g->device, nullptr, &g->context);
		adapter->Release();
	}
	if (factory)
		factory->Release();
	if (FAILED(hr))
		hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &g->device, nullptr, &g->context);
	if (FAILED(hr)) {
		*whyNot = "no D3D11 device, not even WARP (" + std::to_string((long)hr) + ")";
		return nullptr;
	}
	return g;
}
