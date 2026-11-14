//
// Created by ZNix on 8/02/2021.
//

#if defined(SUPPORT_DX11)

#include "TemporaryD3D11.h"

static IDXGIAdapter1* d3d_get_adapter(const LUID& adapter_luid)
{
	// Turn the LUID into a specific graphics device adapter
	IDXGIAdapter1* final_adapter = nullptr;
	IDXGIAdapter1* curr_adapter = nullptr;
	IDXGIFactory1* dxgi_factory;
	DXGI_ADAPTER_DESC1 adapter_desc;

	CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&dxgi_factory));

	int curr = 0;
	while (dxgi_factory->EnumAdapters1(curr++, &curr_adapter) == S_OK) {
		curr_adapter->GetDesc1(&adapter_desc);

		if (memcmp(&adapter_desc.AdapterLuid, &adapter_luid, sizeof(LUID)) == 0) {
			final_adapter = curr_adapter;
			break;
		}
		curr_adapter->Release();
		curr_adapter = nullptr;
	}
	dxgi_factory->Release();
	return final_adapter;
}

TemporaryD3D11::TemporaryD3D11()
{
	// The spec requires that we call this first, and use it to get the correct things
	XrGraphicsRequirementsD3D11KHR graphicsRequirements{};
	graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
	XrResult res = xr_ext->xrGetD3D11GraphicsRequirementsKHR(xr_instance, xr_system, &graphicsRequirements);
	if (XR_FAILED(res)) {
		OOVR_LOGF("xrGetD3D11GraphicsRequirementsKHR failed: %d", (int)res);
		return;
	}

	IDXGIAdapter1* adapter = d3d_get_adapter(graphicsRequirements.adapterLuid);
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

	if (graphicsRequirements.minFeatureLevel > D3D_FEATURE_LEVEL_11_0)
		featureLevels[0] = graphicsRequirements.minFeatureLevel;

	// Such a horrid hack - of all the ugly things we do in OpenComposite, this has to be one of the worst.
	HRESULT createDeviceRes = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
	    featureLevels, _countof(featureLevels),
	    D3D11_SDK_VERSION, &device, nullptr, nullptr);

	if (adapter)
		adapter->Release();
	if (FAILED(createDeviceRes) || !device) {
		OOVR_LOGF("D3D11CreateDevice for the OpenXR runtime's adapter failed: 0x%08lx", (unsigned long)createDeviceRes);
		device = nullptr;
		return;
	}

	d3dInfo = XrGraphicsBindingD3D11KHR{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
	d3dInfo.device = device;
	usable = true;
}

TemporaryD3D11::~TemporaryD3D11()
{
	if (device)
		device->Release();
}

#endif
