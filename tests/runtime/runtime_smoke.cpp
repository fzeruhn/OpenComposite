// Smoke tests of the stand-in runtime (oc_test_runtime) through the OpenXR loader: instance, system, a D3D11 session,
// frames with layers, then the dump read back. Run with XR_RUNTIME_JSON set to the runtime's manifest and
// OC_TEST_RUNTIME_DUMP to a file path (tests/runtime/CMakeLists.txt does both). script_formats_and_poses needs
// scripts/formats_poses.json as OC_TEST_RUNTIME_SCRIPT, and runs on its own.
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
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <json/json.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#define XR_OK(expr) CHECK(XR_SUCCEEDED(expr))

namespace {

const char* DumpPath()
{
	const char* p = std::getenv("OC_TEST_RUNTIME_DUMP");
	return p ? p : "";
}

Json::Value ReadDump()
{
	Json::Value dump;
	std::ifstream in(DumpPath());
	Json::CharReaderBuilder rb;
	std::string errs;
	CHECK(Json::parseFromStream(rb, in, &dump, &errs));
	return dump;
}

// An instance, a D3D11 device on the runtime's adapter, a running session and a STAGE space
struct Xr {
	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	XrSession session = XR_NULL_HANDLE;
	XrSpace stage = XR_NULL_HANDLE;
	std::vector<XrSwapchain> swapchains;

	Xr()
	{
		REQUIRE(*DumpPath());
		std::remove(DumpPath());
		const char* exts[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
		XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
		std::snprintf(ici.applicationInfo.applicationName, sizeof(ici.applicationInfo.applicationName), "runtime_smoke");
		ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
		ici.enabledExtensionCount = 1;
		ici.enabledExtensionNames = exts;
		REQUIRE(XR_SUCCEEDED(xrCreateInstance(&ici, &instance)));

		XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
		sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
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
		factory->Release();
		REQUIRE(adapter != nullptr);
		D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
		adapter->Release();
		REQUIRE(device != nullptr);

		XrGraphicsBindingD3D11KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
		binding.device = device;
		XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
		sci.next = &binding;
		sci.systemId = system;
		REQUIRE(XR_SUCCEEDED(xrCreateSession(instance, &sci, &session)));

		XrSessionBeginInfo sbi{ XR_TYPE_SESSION_BEGIN_INFO };
		sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		XR_OK(xrBeginSession(session, &sbi));

		XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		rsci.poseInReferenceSpace.orientation.w = 1;
		XR_OK(xrCreateReferenceSpace(session, &rsci, &stage));
	}

	XrSwapchain Swapchain(uint32_t w, uint32_t h, int64_t format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
	{
		XrSwapchainCreateInfo swci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
		swci.format = format;
		swci.sampleCount = 1;
		swci.width = w;
		swci.height = h;
		swci.faceCount = 1;
		swci.arraySize = 1;
		swci.mipCount = 1;
		XrSwapchain sc = XR_NULL_HANDLE;
		XR_OK(xrCreateSwapchain(session, &swci, &sc));
		swapchains.push_back(sc);
		return sc;
	}

	// Acquires, waits on, clears to colour (RGBA, 0-1) and releases the next image
	void Fill(XrSwapchain sc, const float colour[4])
	{
		uint32_t count = 0;
		XR_OK(xrEnumerateSwapchainImages(sc, 0, &count, nullptr));
		std::vector<XrSwapchainImageD3D11KHR> images(count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
		XR_OK(xrEnumerateSwapchainImages(sc, count, &count, (XrSwapchainImageBaseHeader*)images.data()));
		uint32_t index = 0;
		XR_OK(xrAcquireSwapchainImage(sc, nullptr, &index));
		XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wi.timeout = XR_INFINITE_DURATION;
		XR_OK(xrWaitSwapchainImage(sc, &wi));
		ID3D11RenderTargetView* rtv = nullptr;
		D3D11_TEXTURE2D_DESC desc;
		images[index].texture->GetDesc(&desc);
		D3D11_RENDER_TARGET_VIEW_DESC rd{};
		rd.Format = desc.Format;
		rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		if (SUCCEEDED(device->CreateRenderTargetView(images[index].texture, &rd, &rtv))) {
			context->ClearRenderTargetView(rtv, colour);
			rtv->Release();
		}
		XR_OK(xrReleaseSwapchainImage(sc, nullptr));
	}

	XrFrameState BeginFrame()
	{
		XrFrameState fs{ XR_TYPE_FRAME_STATE };
		XR_OK(xrWaitFrame(session, nullptr, &fs));
		XR_OK(xrBeginFrame(session, nullptr));
		return fs;
	}

	void EndFrame(const XrFrameState& fs, std::vector<const XrCompositionLayerBaseHeader*> layers)
	{
		XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
		fei.displayTime = fs.predictedDisplayTime;
		fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		fei.layerCount = (uint32_t)layers.size();
		fei.layers = layers.data();
		XR_OK(xrEndFrame(session, &fei));
	}

	// Ends the session and the instance, and reads the dump the runtime wrote
	Json::Value Finish()
	{
		for (XrSwapchain sc : swapchains)
			xrDestroySwapchain(sc);
		swapchains.clear();
		xrDestroySpace(stage);
		if (XR_SUCCEEDED(xrRequestExitSession(session)))
			xrEndSession(session);
		xrDestroySession(session);
		xrDestroyInstance(instance);
		instance = XR_NULL_HANDLE;
		if (context)
			context->Release();
		device->Release();
		device = nullptr;
		return ReadDump();
	}

	~Xr()
	{
		if (instance != XR_NULL_HANDLE)
			Finish();
	}
};

bool Contains(const Json::Value& list, const std::string& s)
{
	for (const Json::Value& v : list)
		if (v.asString() == s)
			return true;
	return false;
}

} // namespace

TEST_CASE("ten_projection_frames")
{
	Xr xr;
	XrInstanceProperties props{ XR_TYPE_INSTANCE_PROPERTIES };
	XR_OK(xrGetInstanceProperties(xr.instance, &props));
	CHECK(std::string(props.runtimeName) == "OpenComposite test runtime");

	XrSwapchain swapchain = xr.Swapchain(256, 256);
	uint32_t imageCount = 0;
	XR_OK(xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr));
	CHECK(imageCount >= 2);

	for (int frame = 0; frame < 10; frame++) {
		XrFrameState fs = xr.BeginFrame();
		XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
		vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		vli.displayTime = fs.predictedDisplayTime;
		vli.space = xr.stage;
		XrViewState vs{ XR_TYPE_VIEW_STATE };
		XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
		uint32_t viewCount = 0;
		XR_OK(xrLocateViews(xr.session, &vli, &vs, 2, &viewCount, views));

		const float grey[4] = { 0.5f, 0.5f, 0.5f, 1 };
		xr.Fill(swapchain, grey);

		XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
		for (int e = 0; e < 2; e++) {
			pv[e].pose = views[e].pose;
			pv[e].fov = views[e].fov;
			pv[e].subImage.swapchain = swapchain;
			pv[e].subImage.imageRect.extent = { 256, 256 };
		}
		XrCompositionLayerProjection proj{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
		proj.space = xr.stage;
		proj.viewCount = 2;
		proj.views = pv;
		xr.EndFrame(fs, { (XrCompositionLayerBaseHeader*)&proj });
	}

	// The session ends only once it's STOPPING
	CHECK(xrEndSession(xr.session) == XR_ERROR_SESSION_NOT_STOPPING);
	Json::Value dump = xr.Finish();
	CHECK(dump["frames_ended"].asInt() == 10);
	CHECK(dump["layers"].size() == 10);
	CHECK(dump["layers"][9]["type"].asString() == "projection");
	CHECK(dump["session"]["binding"].asString() == "d3d11");
	CHECK(dump["frame_order_errors"].asInt() == 0);
}

TEST_CASE("layer_record_has_pose_and_samples")
{
	Xr xr;
	XrSwapchain swapchain = xr.Swapchain(256, 256);
	XrFrameState fs = xr.BeginFrame();
	const float red[4] = { 1, 0, 0, 1 };
	xr.Fill(swapchain, red);
	XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
	for (int e = 0; e < 2; e++) {
		pv[e].pose = { { 0, 0, 0, 1 }, { e == 0 ? -0.03f : 0.03f, 1.7f, 0 } };
		pv[e].fov = { -0.7f, 0.6f, 0.5f, -0.4f };
		pv[e].subImage.swapchain = swapchain;
		pv[e].subImage.imageRect.extent = { 256, 256 };
	}
	XrCompositionLayerProjection proj{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	proj.space = xr.stage;
	proj.viewCount = 2;
	proj.views = pv;
	xr.EndFrame(fs, { (XrCompositionLayerBaseHeader*)&proj });

	Json::Value dump = xr.Finish();
	REQUIRE(dump["layers"].size() == 1);
	const Json::Value& layer = dump["layers"][0];
	CHECK(layer["type"].asString() == "projection");
	CHECK(layer["space"].asString() == "stage");
	CHECK(layer["invalid"].size() == 0);
	REQUIRE(layer["views"].size() == 2);
	const Json::Value& v0 = layer["views"][0];
	CHECK(v0["samples"]["c"].asString() == "ff0000ff");
	CHECK(v0["samples"]["tl"].asString() == "ff0000ff");
	CHECK(v0["pose"]["p"].size() == 3);
	CHECK(v0["pose"]["p"][0].asFloat() == doctest::Approx(-0.03f));
	CHECK(v0["pose"]["p"][1].asFloat() == doctest::Approx(1.7f));
	CHECK(v0["pose"]["q"].size() == 4);
	REQUIRE(v0["fov"].size() == 4);
	CHECK(v0["fov"][0].asFloat() == doctest::Approx(-0.7f));
	CHECK(v0["fov"][3].asFloat() == doctest::Approx(-0.4f));
	CHECK(v0["rect"][2].asInt() == 256);
	CHECK(v0["format"].asInt() == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	CHECK(v0["depth"].isNull());
}

TEST_CASE("negative_extent_is_invalid")
{
	Xr xr;
	XrSwapchain swapchain = xr.Swapchain(128, 128);
	XrFrameState fs = xr.BeginFrame();
	const float blue[4] = { 0, 0, 1, 1 };
	xr.Fill(swapchain, blue);
	XrCompositionLayerQuad quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
	quad.space = xr.stage;
	quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	quad.pose = { { 0, 0, 0, 1 }, { 0, 1.5f, -2 } };
	quad.size = { 1, 1 };
	quad.subImage.swapchain = swapchain;
	quad.subImage.imageRect.extent = { 128, -100 };
	xr.EndFrame(fs, { (XrCompositionLayerBaseHeader*)&quad });

	Json::Value dump = xr.Finish();
	REQUIRE(dump["layers"].size() == 1);
	const Json::Value& layer = dump["layers"][0];
	CHECK(layer["type"].asString() == "quad");
	CHECK(Contains(layer["invalid"], "negative extent"));
	CHECK(layer["size"][0].asFloat() == doctest::Approx(1.0f));
	CHECK(layer["eye_visibility"].asString() == "both");
	CHECK(layer["pose"]["p"][2].asFloat() == doctest::Approx(-2.0f));
}

TEST_CASE("frame_order_counted")
{
	Xr xr;
	XrFrameState fs{ XR_TYPE_FRAME_STATE };
	XR_OK(xrWaitFrame(xr.session, nullptr, &fs));
	XR_OK(xrBeginFrame(xr.session, nullptr));
	CHECK(xrBeginFrame(xr.session, nullptr) == XR_ERROR_CALL_ORDER_INVALID);
	xr.EndFrame(fs, {});
	Json::Value dump = xr.Finish();
	CHECK(dump["frame_order_errors"].asInt() == 1);
}

TEST_CASE("script_formats_and_poses" * doctest::skip(!std::getenv("OC_TEST_RUNTIME_SCRIPT")))
{
	Xr xr;
	uint32_t count = 0;
	XR_OK(xrEnumerateSwapchainFormats(xr.session, 0, &count, nullptr));
	std::vector<int64_t> formats(count);
	XR_OK(xrEnumerateSwapchainFormats(xr.session, count, &count, formats.data()));
	REQUIRE(count == 1);
	CHECK(formats[0] == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

	XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
	std::snprintf(asci.actionSetName, sizeof(asci.actionSetName), "main");
	std::snprintf(asci.localizedActionSetName, sizeof(asci.localizedActionSetName), "main");
	XrActionSet set = XR_NULL_HANDLE;
	XR_OK(xrCreateActionSet(xr.instance, &asci, &set));
	XrPath left = XR_NULL_PATH, aimPath = XR_NULL_PATH, profile = XR_NULL_PATH;
	XR_OK(xrStringToPath(xr.instance, "/user/hand/left", &left));
	XR_OK(xrStringToPath(xr.instance, "/user/hand/left/input/aim/pose", &aimPath));
	XR_OK(xrStringToPath(xr.instance, "/interaction_profiles/khr/simple_controller", &profile));
	XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
	std::snprintf(aci.actionName, sizeof(aci.actionName), "aim");
	std::snprintf(aci.localizedActionName, sizeof(aci.localizedActionName), "aim");
	aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
	aci.countSubactionPaths = 1;
	aci.subactionPaths = &left;
	XrAction aim = XR_NULL_HANDLE;
	XR_OK(xrCreateAction(set, &aci, &aim));
	XrActionSuggestedBinding sb{ aim, aimPath };
	XrInteractionProfileSuggestedBinding ipsb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	ipsb.interactionProfile = profile;
	ipsb.countSuggestedBindings = 1;
	ipsb.suggestedBindings = &sb;
	XR_OK(xrSuggestInteractionProfileBindings(xr.instance, &ipsb));
	XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach.countActionSets = 1;
	attach.actionSets = &set;
	XR_OK(xrAttachSessionActionSets(xr.session, &attach));

	XrActionSpaceCreateInfo spci{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
	spci.action = aim;
	spci.subactionPath = left;
	spci.poseInActionSpace.orientation.w = 1;
	XrSpace aimSpace = XR_NULL_HANDLE;
	XR_OK(xrCreateActionSpace(xr.session, &spci, &aimSpace));

	XrFrameState fs = xr.BeginFrame();
	XrActiveActionSet active{ set, XR_NULL_PATH };
	XrActionsSyncInfo sync{ XR_TYPE_ACTIONS_SYNC_INFO };
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	XR_OK(xrSyncActions(xr.session, &sync));

	XrSpaceVelocity vel{ XR_TYPE_SPACE_VELOCITY };
	XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
	loc.next = &vel;
	XR_OK(xrLocateSpace(aimSpace, xr.stage, fs.predictedDisplayTime, &loc));
	CHECK(loc.pose.position.x == doctest::Approx(0.1f));
	CHECK(loc.pose.position.y == doctest::Approx(1.2f));
	CHECK(loc.pose.position.z == doctest::Approx(-0.4f));
	CHECK((vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0);
	CHECK(vel.linearVelocity.x == doctest::Approx(1.0f));
	CHECK(vel.angularVelocity.y == doctest::Approx(2.0f));
	xr.EndFrame(fs, {});
	xrDestroySpace(aimSpace);
	xrDestroyActionSet(set);
	xr.Finish();
}
