// D3D10 for the frame cases: a device on the first adapter (WARP without one); textures made with the pattern as
// their initial data. A D3D10 texture is submitted as TextureType_DirectX, as D3D11's.

#include "GfxApi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d10_1.h>

namespace {

class D3D10Gfx : public GfxApi {
public:
	ID3D10Device1* device = nullptr;
	std::vector<ID3D10Texture2D*> made;

	~D3D10Gfx() override
	{
		for (ID3D10Texture2D* t : made)
			t->Release();
		if (device)
			device->Release();
	}

	GfxTexture Wrap(ID3D10Texture2D* t)
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
		if (texels == gfx::Texels::Unknown || !w || !h || samples > 1)
			return Wrap(nullptr); // multisampled patterns: D3D11 only for now
		D3D10_TEXTURE2D_DESC desc{};
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = 1;
		desc.ArraySize = arraySize ? arraySize : 1;
		desc.Format = (DXGI_FORMAT)nativeFormat;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D10_USAGE_DEFAULT;
		desc.BindFlags = D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_RENDER_TARGET;
		size_t pitch = (size_t)w * gfx::BytesOf(texels);
		std::vector<uint8_t> data = gfx::PatternTexels(texels, w, h, eye, pitch, false);
		std::vector<D3D10_SUBRESOURCE_DATA> init(desc.ArraySize, { data.data(), (UINT)pitch, 0 });
		ID3D10Texture2D* t = nullptr;
		if (FAILED(device->CreateTexture2D(&desc, init.data(), &t)))
			t = nullptr;
		return Wrap(t);
	}

	GfxTexture Depth(uint32_t w, uint32_t h, float value) override
	{
		D3D10_TEXTURE2D_DESC desc{};
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D10_USAGE_DEFAULT;
		desc.BindFlags = D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_DEPTH_STENCIL;
		std::vector<float> data((size_t)w * h, value);
		D3D10_SUBRESOURCE_DATA init{ data.data(), w * 4, 0 };
		ID3D10Texture2D* t = nullptr;
		if (FAILED(device->CreateTexture2D(&desc, &init, &t)))
			t = nullptr;
		return Wrap(t);
	}

	void Flush() override
	{
		device->Flush();
		D3D10_QUERY_DESC qd{ D3D10_QUERY_EVENT, 0 };
		ID3D10Query* q = nullptr;
		if (SUCCEEDED(device->CreateQuery(&qd, &q))) {
			q->End();
			BOOL done = FALSE;
			for (int i = 0; i < 5000 && q->GetData(&done, sizeof(done), 0) != S_OK; i++)
				Sleep(1); // at most about 5 s
			q->Release();
		}
	}
};

} // namespace

std::unique_ptr<GfxApi> MakeD3D10Gfx(std::string* whyNot)
{
	auto g = std::make_unique<D3D10Gfx>();
	// The first adapter, as for D3D11
	HRESULT hr = E_FAIL;
	IDXGIFactory1* factory = nullptr;
	IDXGIAdapter1* adapter = nullptr;
	if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) && factory->EnumAdapters1(0, &adapter) == S_OK) {
		hr = D3D10CreateDevice1(adapter, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0, D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION, &g->device);
		adapter->Release();
	}
	if (factory)
		factory->Release();
	if (FAILED(hr))
		hr = D3D10CreateDevice1(nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, 0, D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION, &g->device);
	if (FAILED(hr)) {
		*whyNot = "no D3D10 device, not even WARP (" + std::to_string((long)hr) + ")";
		return nullptr;
	}
	return g;
}
