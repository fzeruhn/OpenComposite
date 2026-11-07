#include "runtime_internal.h"

#include <dxgi1_2.h>

XrResult RtD3D11Requirements(XrGraphicsRequirementsD3D11KHR* reqs)
{
	IDXGIFactory1* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
		return XR_ERROR_RUNTIME_FAILURE;
	IDXGIAdapter1* adapter = nullptr;
	XrResult result = XR_ERROR_RUNTIME_FAILURE;
	if (factory->EnumAdapters1(0, &adapter) == S_OK) {
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		reqs->adapterLuid = desc.AdapterLuid;
		reqs->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
		adapter->Release();
		result = XR_SUCCESS;
	}
	factory->Release();
	return result;
}

const std::vector<int64_t>& RtD3D11Formats()
{
	static const std::vector<int64_t> formats = {
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_B8G8R8A8_UNORM,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_D32_FLOAT,
	};
	return formats;
}

XrResult RtD3D11CreateSwapchain(RtSession* session, const XrSwapchainCreateInfo* info, RtSwapchain* sc)
{
	if (!session->device)
		return XR_ERROR_VALIDATION_FAILURE;
	bool known = false;
	for (int64_t f : RtD3D11Formats())
		known = known || f == info->format;
	if (!known)
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;

	bool depth = info->format == DXGI_FORMAT_D32_FLOAT;
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = info->width;
	desc.Height = info->height;
	desc.MipLevels = info->mipCount ? info->mipCount : 1;
	desc.ArraySize = (info->arraySize ? info->arraySize : 1) * (info->faceCount ? info->faceCount : 1);
	desc.Format = (DXGI_FORMAT)info->format;
	desc.SampleDesc.Count = info->sampleCount ? info->sampleCount : 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = depth ? D3D11_BIND_DEPTH_STENCIL : (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

	sc->info = *info;
	for (int i = 0; i < 3; i++) {
		ID3D11Texture2D* tex = nullptr;
		if (FAILED(session->device->CreateTexture2D(&desc, nullptr, &tex))) {
			RtD3D11DestroySwapchain(sc);
			return XR_ERROR_RUNTIME_FAILURE;
		}
		sc->textures.push_back(tex);
	}
	return XR_SUCCESS;
}

void RtD3D11DestroySwapchain(RtSwapchain* sc)
{
	for (ID3D11Texture2D* tex : sc->textures)
		tex->Release();
	sc->textures.clear();
}
