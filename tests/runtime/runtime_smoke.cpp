// Smoke test of the stand-in runtime (oc_test_runtime) through the OpenXR loader: instance, system, a D3D11 session and
// ten frames with one projection layer, then the dump is read back. Run with XR_RUNTIME_JSON set to the runtime's
// manifest and OC_TEST_RUNTIME_DUMP to a file path (tests/CMakeLists.txt does both).
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
// The project defines every graphics API for every target; this test only does D3D11
#undef XR_USE_GRAPHICS_API_VULKAN
#undef XR_USE_GRAPHICS_API_OPENGL
#undef XR_USE_GRAPHICS_API_OPENGL_ES
#undef XR_USE_GRAPHICS_API_D3D12
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <json/json.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static int failures = 0;
#define CHECK(cond)                                                     \
	do {                                                                \
		if (!(cond)) {                                                  \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                 \
		}                                                               \
	} while (0)
#define XR_OK(expr) CHECK(XR_SUCCEEDED(expr))

int main()
{
	const char* dumpPath = std::getenv("OC_TEST_RUNTIME_DUMP");
	if (!dumpPath) {
		std::printf("OC_TEST_RUNTIME_DUMP not set\n");
		return 2;
	}
	std::remove(dumpPath);

	const char* exts[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
	std::snprintf(ici.applicationInfo.applicationName, sizeof(ici.applicationInfo.applicationName), "runtime_smoke");
	ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	ici.enabledExtensionCount = 1;
	ici.enabledExtensionNames = exts;
	XrInstance instance = XR_NULL_HANDLE;
	XrResult res = xrCreateInstance(&ici, &instance);
	if (XR_FAILED(res)) {
		std::printf("xrCreateInstance failed: %d\n", res);
		return 1;
	}

	XrInstanceProperties props{ XR_TYPE_INSTANCE_PROPERTIES };
	XR_OK(xrGetInstanceProperties(instance, &props));
	CHECK(std::string(props.runtimeName) == "OpenComposite test runtime");

	XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
	sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	XR_OK(xrGetSystem(instance, &sgi, &system));

	PFN_xrGetD3D11GraphicsRequirementsKHR getReqs = nullptr;
	XR_OK(xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReqs));
	XrGraphicsRequirementsD3D11KHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
	XR_OK(getReqs(instance, system, &reqs));

	IDXGIFactory1* factory = nullptr;
	CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
	IDXGIAdapter1* adapter = nullptr;
	for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; i++) {
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		if (std::memcmp(&desc.AdapterLuid, &reqs.adapterLuid, sizeof(LUID)) == 0)
			break;
		adapter->Release();
		adapter = nullptr;
	}
	CHECK(adapter != nullptr);
	ID3D11Device* device = nullptr;
	D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr);
	CHECK(device != nullptr);

	XrGraphicsBindingD3D11KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
	binding.device = device;
	XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
	sci.next = &binding;
	sci.systemId = system;
	XrSession session = XR_NULL_HANDLE;
	XR_OK(xrCreateSession(instance, &sci, &session));

	XrSessionBeginInfo sbi{ XR_TYPE_SESSION_BEGIN_INFO };
	sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	XR_OK(xrBeginSession(session, &sbi));

	XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	rsci.poseInReferenceSpace.orientation.w = 1;
	XrSpace stage = XR_NULL_HANDLE;
	XR_OK(xrCreateReferenceSpace(session, &rsci, &stage));

	XrSwapchainCreateInfo swci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
	swci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	swci.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	swci.sampleCount = 1;
	swci.width = 256;
	swci.height = 256;
	swci.faceCount = 1;
	swci.arraySize = 1;
	swci.mipCount = 1;
	XrSwapchain swapchain = XR_NULL_HANDLE;
	XR_OK(xrCreateSwapchain(session, &swci, &swapchain));
	uint32_t imageCount = 0;
	XR_OK(xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr));
	std::vector<XrSwapchainImageD3D11KHR> images(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
	XR_OK(xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount, (XrSwapchainImageBaseHeader*)images.data()));
	CHECK(imageCount >= 2);
	CHECK(images[0].texture != nullptr);

	for (int frame = 0; frame < 10; frame++) {
		XrFrameState fs{ XR_TYPE_FRAME_STATE };
		XR_OK(xrWaitFrame(session, nullptr, &fs));
		XR_OK(xrBeginFrame(session, nullptr));

		XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
		vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		vli.displayTime = fs.predictedDisplayTime;
		vli.space = stage;
		XrViewState vs{ XR_TYPE_VIEW_STATE };
		XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
		uint32_t viewCount = 0;
		XR_OK(xrLocateViews(session, &vli, &vs, 2, &viewCount, views));

		uint32_t index = 0;
		XR_OK(xrAcquireSwapchainImage(swapchain, nullptr, &index));
		XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wi.timeout = XR_INFINITE_DURATION;
		XR_OK(xrWaitSwapchainImage(swapchain, &wi));
		XR_OK(xrReleaseSwapchainImage(swapchain, nullptr));

		XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
		for (int e = 0; e < 2; e++) {
			pv[e].pose = views[e].pose;
			pv[e].fov = views[e].fov;
			pv[e].subImage.swapchain = swapchain;
			pv[e].subImage.imageRect.extent = { 256, 256 };
		}
		XrCompositionLayerProjection proj{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
		proj.space = stage;
		proj.viewCount = 2;
		proj.views = pv;
		const XrCompositionLayerBaseHeader* layers[] = { (XrCompositionLayerBaseHeader*)&proj };
		XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
		fei.displayTime = fs.predictedDisplayTime;
		fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		fei.layerCount = 1;
		fei.layers = layers;
		XR_OK(xrEndFrame(session, &fei));
	}

	xrDestroySwapchain(swapchain);
	xrDestroySpace(stage);
	// The session ends only once it's STOPPING
	CHECK(xrEndSession(session) == XR_ERROR_SESSION_NOT_STOPPING);
	XR_OK(xrRequestExitSession(session));
	XR_OK(xrEndSession(session));
	xrDestroySession(session);
	xrDestroyInstance(instance);
	device->Release();

	Json::Value dump;
	std::ifstream in(dumpPath);
	Json::CharReaderBuilder rb;
	std::string errs;
	CHECK(Json::parseFromStream(rb, in, &dump, &errs));
	CHECK(dump["frames_ended"].asInt() == 10);
	CHECK(dump["layers"].size() == 10);
	CHECK(dump["layers"][9]["type"].asString() == "projection");
	CHECK(dump["session"]["binding"].asString() == "d3d11");

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "passed", failures);
	return failures ? 1 : 0;
}
