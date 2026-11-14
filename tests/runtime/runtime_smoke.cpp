// Smoke tests of the stand-in runtime (oc_test_runtime) through the OpenXR loader: instance, system, a session,
// frames with layers, then the dump read back. Run with XR_RUNTIME_JSON set to the runtime's manifest and
// OC_TEST_RUNTIME_DUMP to a file path (tests/runtime/CMakeLists.txt does both). The script tests run on their own,
// with a script as OC_TEST_RUNTIME_SCRIPT: script_formats_and_poses with scripts/formats_poses.json, the *_red_frame
// (each API) and *_session_checks ones with scripts/all_apis.json.
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#ifndef XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_D3D12
#endif
#ifndef XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_GRAPHICS_API_VULKAN
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#undef XR_USE_GRAPHICS_API_OPENGL_ES
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <GL/gl.h>
// Vulkan's functions are looked up at run time: no vulkan-1.dll is needed for the other tests
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <json/json.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "opengl32.lib")

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

TEST_CASE("script_formats_and_poses")
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

TEST_CASE("dump_keeps_bounded_layers")
{
	// 200 frames: whole records for 1-64, 96, 128, 160 and 192, and the last one; the rest only counted
	Xr xr;
	XrSwapchain swapchain = xr.Swapchain(64, 64);
	const float blue[4] = { 0, 0, 1, 1 };
	for (int frame = 1; frame <= 200; frame++) {
		XrFrameState fs = xr.BeginFrame();
		xr.Fill(swapchain, blue);
		XrCompositionLayerQuad quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
		quad.space = xr.stage;
		quad.pose = { { 0, 0, 0, 1 }, { 0, 1.5f, -2 } };
		quad.size = { 1, 1 };
		quad.subImage.swapchain = swapchain;
		quad.subImage.imageRect.extent = { 64, 64 };
		xr.EndFrame(fs, { (XrCompositionLayerBaseHeader*)&quad });
	}
	Json::Value dump = xr.Finish();
	CHECK(dump["frames_ended"].asInt() == 200);
	REQUIRE(dump["layers"].size() == 69);
	CHECK(dump["layers_dropped"].asInt() == 131);
	CHECK(dump["layers"][64]["frame"].asInt() == 96);
	CHECK(dump["layers"][68]["frame"].asInt() == 200);
	CHECK(dump["layers"][64].isMember("samples"));
	CHECK(!dump["layers"][68].isMember("samples")); // pixels only for the frames the rule keeps
}

TEST_CASE("frame_checks_recorded")
{
	Xr xr;
	XrFrameState fs = xr.BeginFrame();
	const XrCompositionLayerBaseHeader* layers[] = { nullptr };
	XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
	fei.displayTime = 0;
	fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
	fei.layerCount = 1;
	fei.layers = layers;
	XR_OK(xrEndFrame(xr.session, &fei));
	(void)fs;
	Json::Value dump = xr.Finish();
	REQUIRE(dump["layers"].size() == 1);
	CHECK(Contains(dump["layers"][0]["invalid"], "null layer"));
	CHECK(dump["frame_invalid"]["display time not positive"].asInt() == 1);
	CHECK(dump["frame_invalid"]["blend mode unsupported"].asInt() == 1);
}

// --- a frame on each API (scripts/all_apis.json offers them), and the session checks a real runtime makes ---

namespace {

// A device of one API for a session: made after the instance (its requirements first), filled with raw bytes
struct Gpu {
	virtual ~Gpu() = default;
	virtual const char* Extension() = 0;
	virtual const char* BindingName() = 0;
	virtual bool Make(XrInstance instance, XrSystemId system) = 0;
	virtual const void* Binding() = 0;
	virtual int64_t Format() = 0; // RGBA8 sRGB in the API's enum
	virtual bool BottomUp() { return false; }
	// Writes 256x256 RGBA8 bytes (rows in the image's own order) into image index, unconverted
	virtual void Upload(XrSwapchain sc, uint32_t index, const std::vector<uint8_t>& rgba) = 0;
};

template <typename T>
T XrFn(XrInstance instance, const char* name)
{
	PFN_xrVoidFunction fn = nullptr;
	xrGetInstanceProcAddr(instance, name, &fn);
	return (T)fn;
}

const uint32_t kSize = 256;

// The top half red, the bottom half mid-grey (0x80: sRGB-encoded bytes, read back as they are), as shown
std::vector<uint8_t> TwoTone(bool bottomUp)
{
	std::vector<uint8_t> data(kSize * kSize * 4);
	for (uint32_t row = 0; row < kSize; row++) {
		bool top = (bottomUp ? kSize - 1 - row : row) < kSize / 2;
		for (uint32_t x = 0; x < kSize; x++) {
			uint8_t* p = &data[(row * kSize + x) * 4];
			p[0] = top ? 0xff : 0x80, p[1] = top ? 0 : 0x80, p[2] = top ? 0 : 0x80, p[3] = 0xff;
		}
	}
	return data;
}

struct D3D11Gpu : Gpu {
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	XrGraphicsBindingD3D11KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };

	~D3D11Gpu() override
	{
		if (context)
			context->Release();
		if (device)
			device->Release();
	}
	const char* Extension() override { return XR_KHR_D3D11_ENABLE_EXTENSION_NAME; }
	const char* BindingName() override { return "d3d11"; }
	int64_t Format() override { return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }
	const void* Binding() override { return &binding; }

	bool Make(XrInstance instance, XrSystemId system) override
	{
		auto getReqs = XrFn<PFN_xrGetD3D11GraphicsRequirementsKHR>(instance, "xrGetD3D11GraphicsRequirementsKHR");
		XrGraphicsRequirementsD3D11KHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
		if (!getReqs || XR_FAILED(getReqs(instance, system, &reqs)))
			return false;
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
		if (!adapter)
			return false;
		D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
		adapter->Release();
		binding.device = device;
		return device != nullptr;
	}

	void Upload(XrSwapchain sc, uint32_t index, const std::vector<uint8_t>& rgba) override
	{
		uint32_t count = 0;
		xrEnumerateSwapchainImages(sc, 0, &count, nullptr);
		std::vector<XrSwapchainImageD3D11KHR> images(count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
		REQUIRE(XR_SUCCEEDED(xrEnumerateSwapchainImages(sc, count, &count, (XrSwapchainImageBaseHeader*)images.data())));
		context->UpdateSubresource(images[index].texture, 0, nullptr, rgba.data(), kSize * 4, 0);
	}
};

struct D3D12Gpu : Gpu {
	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* queue = nullptr;
	XrGraphicsBindingD3D12KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D12_KHR };
	bool warp = false; // a WARP device whatever the requirements say

	~D3D12Gpu() override
	{
		if (queue)
			queue->Release();
		if (device)
			device->Release();
	}
	const char* Extension() override { return XR_KHR_D3D12_ENABLE_EXTENSION_NAME; }
	const char* BindingName() override { return "d3d12"; }
	int64_t Format() override { return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }
	const void* Binding() override { return &binding; }

	bool Make(XrInstance instance, XrSystemId system) override
	{
		auto getReqs = XrFn<PFN_xrGetD3D12GraphicsRequirementsKHR>(instance, "xrGetD3D12GraphicsRequirementsKHR");
		XrGraphicsRequirementsD3D12KHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR };
		if (!getReqs || XR_FAILED(getReqs(instance, system, &reqs)))
			return false;
		IDXGIFactory4* factory = nullptr;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory)))
			return false;
		IDXGIAdapter1* adapter = nullptr;
		for (UINT i = 0; !warp && factory->EnumAdapters1(i, &adapter) == S_OK; i++) {
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);
			if (std::memcmp(&desc.AdapterLuid, &reqs.adapterLuid, sizeof(LUID)) == 0)
				break;
			adapter->Release();
			adapter = nullptr;
		}
		if (adapter && FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
			device = nullptr;
		if (!device) {
			// The requirements' adapter can't (CI's runner without a GPU), or warp was asked for: WARP
			IDXGIAdapter* w = nullptr;
			if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&w)))) {
				D3D12CreateDevice(w, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
				w->Release();
			}
		}
		if (adapter)
			adapter->Release();
		factory->Release();
		if (!device)
			return false;
		D3D12_COMMAND_QUEUE_DESC qd{};
		qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))))
			return false;
		binding.device = device;
		binding.queue = queue;
		return true;
	}

	void Upload(XrSwapchain sc, uint32_t index, const std::vector<uint8_t>& rgba) override
	{
		uint32_t count = 0;
		xrEnumerateSwapchainImages(sc, 0, &count, nullptr);
		std::vector<XrSwapchainImageD3D12KHR> images(count, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
		REQUIRE(XR_SUCCEEDED(xrEnumerateSwapchainImages(sc, count, &count, (XrSwapchainImageBaseHeader*)images.data())));
		ID3D12Resource* tex = images[index].texture;
		D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_UPLOAD };
		D3D12_RESOURCE_DESC bd{};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bd.Width = rgba.size();
		bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
		bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource* upload = nullptr;
		REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))));
		void* mapped = nullptr;
		upload->Map(0, nullptr, &mapped);
		std::memcpy(mapped, rgba.data(), rgba.size());
		upload->Unmap(0, nullptr);
		ID3D12CommandAllocator* alloc = nullptr;
		ID3D12GraphicsCommandList* list = nullptr;
		ID3D12Fence* fence = nullptr;
		REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
		REQUIRE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list))));
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = tex;
		b.Transition.Subresource = 0;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		list->ResourceBarrier(1, &b);
		D3D12_TEXTURE_COPY_LOCATION src{};
		src.pResource = upload;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSize, kSize, 1, kSize * 4 };
		D3D12_TEXTURE_COPY_LOCATION dst{};
		dst.pResource = tex;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
		list->ResourceBarrier(1, &b);
		list->Close();
		ID3D12CommandList* lists[] = { list };
		queue->ExecuteCommandLists(1, lists);
		REQUIRE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
		HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		queue->Signal(fence, 1);
		fence->SetEventOnCompletion(1, event);
		CHECK(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0);
		CloseHandle(event);
		fence->Release();
		list->Release();
		alloc->Release();
		upload->Release();
	}
};

PFN_vkGetInstanceProcAddr VkLoader()
{
	HMODULE m = LoadLibraryA("vulkan-1.dll");
	return m ? (PFN_vkGetInstanceProcAddr)GetProcAddress(m, "vkGetInstanceProcAddr") : nullptr;
}

VkInstance MakeVkInstance(PFN_vkGetInstanceProcAddr gipa)
{
	VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.pApplicationInfo = &app;
	VkInstance instance = VK_NULL_HANDLE;
	auto create = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
	if (!create || create(&ici, nullptr, &instance) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return instance;
}

// A Vulkan driver with a physical device (CI has none)
bool VulkanUsable()
{
	PFN_vkGetInstanceProcAddr gipa = VkLoader();
	if (!gipa)
		return false;
	VkInstance instance = MakeVkInstance(gipa);
	if (!instance)
		return false;
	auto enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
	auto destroy = (PFN_vkDestroyInstance)gipa(instance, "vkDestroyInstance");
	uint32_t count = 0;
	bool ok = enumerate && enumerate(instance, &count, nullptr) == VK_SUCCESS && count > 0;
	destroy(instance, nullptr);
	return ok;
}

// enable2: through xrCreateVulkanInstanceKHR, xrGetVulkanGraphicsDevice2KHR and xrCreateVulkanDeviceKHR
struct VulkanGpu : Gpu {
	bool enable2 = false;
	PFN_vkGetInstanceProcAddr gipa = VkLoader();
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t family = 0;
	XrGraphicsBindingVulkanKHR binding{ XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };

	explicit VulkanGpu(bool two = false) : enable2(two) {}
	template <typename T>
	T I(const char* name) { return (T)gipa(instance, name); }
	template <typename T>
	T D(const char* name) { return (T)I<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr")(device, name); }

	~VulkanGpu() override
	{
		if (device)
			D<PFN_vkDestroyDevice>("vkDestroyDevice")(device, nullptr);
		if (instance)
			I<PFN_vkDestroyInstance>("vkDestroyInstance")(instance, nullptr);
	}
	const char* Extension() override { return enable2 ? XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME : XR_KHR_VULKAN_ENABLE_EXTENSION_NAME; }
	const char* BindingName() override { return "vulkan"; }
	int64_t Format() override { return VK_FORMAT_R8G8B8A8_SRGB; }
	const void* Binding() override { return &binding; }

	bool Make(XrInstance xrInstance, XrSystemId system) override
	{
		if (!gipa)
			return false;
		auto getReqs = XrFn<PFN_xrGetVulkanGraphicsRequirementsKHR>(xrInstance,
		    enable2 ? "xrGetVulkanGraphicsRequirements2KHR" : "xrGetVulkanGraphicsRequirementsKHR");
		XrGraphicsRequirementsVulkanKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
		if (!getReqs || XR_FAILED(getReqs(xrInstance, system, &reqs)))
			return false;
		VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		ici.pApplicationInfo = &app;
		if (enable2) {
			auto createInstance = XrFn<PFN_xrCreateVulkanInstanceKHR>(xrInstance, "xrCreateVulkanInstanceKHR");
			auto getDevice2 = XrFn<PFN_xrGetVulkanGraphicsDevice2KHR>(xrInstance, "xrGetVulkanGraphicsDevice2KHR");
			if (!createInstance || !getDevice2)
				return false;
			XrVulkanInstanceCreateInfoKHR ci{ XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
			ci.systemId = system;
			ci.pfnGetInstanceProcAddr = gipa;
			ci.vulkanCreateInfo = &ici;
			VkResult vr = VK_ERROR_INITIALIZATION_FAILED;
			if (XR_FAILED(createInstance(xrInstance, &ci, &instance, &vr)) || vr != VK_SUCCESS)
				return false;
			XrVulkanGraphicsDeviceGetInfoKHR gi{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
			gi.systemId = system;
			gi.vulkanInstance = instance;
			if (XR_FAILED(getDevice2(xrInstance, &gi, &physical)) || !physical)
				return false;
		} else {
			auto getDevice = XrFn<PFN_xrGetVulkanGraphicsDeviceKHR>(xrInstance, "xrGetVulkanGraphicsDeviceKHR");
			instance = MakeVkInstance(gipa);
			if (!getDevice || !instance || XR_FAILED(getDevice(xrInstance, system, instance, &physical)) || !physical)
				return false;
		}
		uint32_t n = 0;
		auto families = I<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
		families(physical, &n, nullptr);
		std::vector<VkQueueFamilyProperties> props(n);
		families(physical, &n, props.data());
		for (family = 0; family < n && !(props[family].queueFlags & VK_QUEUE_GRAPHICS_BIT); family++) {
		}
		if (family == n)
			return false;
		float priority = 1;
		VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		qci.queueFamilyIndex = family;
		qci.queueCount = 1;
		qci.pQueuePriorities = &priority;
		VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		dci.queueCreateInfoCount = 1;
		dci.pQueueCreateInfos = &qci;
		if (enable2) {
			auto createDevice = XrFn<PFN_xrCreateVulkanDeviceKHR>(xrInstance, "xrCreateVulkanDeviceKHR");
			XrVulkanDeviceCreateInfoKHR di{ XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
			di.systemId = system;
			di.pfnGetInstanceProcAddr = gipa;
			di.vulkanPhysicalDevice = physical;
			di.vulkanCreateInfo = &dci;
			VkResult vr = VK_ERROR_INITIALIZATION_FAILED;
			if (!createDevice || XR_FAILED(createDevice(xrInstance, &di, &device, &vr)) || vr != VK_SUCCESS)
				return false;
		} else if (I<PFN_vkCreateDevice>("vkCreateDevice")(physical, &dci, nullptr, &device) != VK_SUCCESS) {
			return false;
		}
		D<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device, family, 0, &queue);
		binding.instance = instance;
		binding.physicalDevice = physical;
		binding.device = device;
		binding.queueFamilyIndex = family;
		binding.queueIndex = 0;
		return true;
	}

	void Upload(XrSwapchain sc, uint32_t index, const std::vector<uint8_t>& rgba) override
	{
		uint32_t count = 0;
		xrEnumerateSwapchainImages(sc, 0, &count, nullptr);
		std::vector<XrSwapchainImageVulkanKHR> images(count, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
		REQUIRE(XR_SUCCEEDED(xrEnumerateSwapchainImages(sc, count, &count, (XrSwapchainImageBaseHeader*)images.data())));
		VkImage image = images[index].image;
		// A host-visible staging buffer with the bytes
		VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bci.size = rgba.size();
		bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		VkBuffer buffer = VK_NULL_HANDLE;
		REQUIRE(D<PFN_vkCreateBuffer>("vkCreateBuffer")(device, &bci, nullptr, &buffer) == VK_SUCCESS);
		VkMemoryRequirements req;
		D<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device, buffer, &req);
		VkPhysicalDeviceMemoryProperties mp;
		I<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(physical, &mp);
		uint32_t type = 0;
		const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		for (type = 0; type < mp.memoryTypeCount; type++)
			if ((req.memoryTypeBits & (1u << type)) && (mp.memoryTypes[type].propertyFlags & want) == want)
				break;
		REQUIRE(type < mp.memoryTypeCount);
		VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		REQUIRE(D<PFN_vkAllocateMemory>("vkAllocateMemory")(device, &mai, nullptr, &mem) == VK_SUCCESS);
		D<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device, buffer, mem, 0);
		void* mapped = nullptr;
		D<PFN_vkMapMemory>("vkMapMemory")(device, mem, 0, bci.size, 0, &mapped);
		std::memcpy(mapped, rgba.data(), rgba.size());
		D<PFN_vkUnmapMemory>("vkUnmapMemory")(device, mem);

		VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		pci.queueFamilyIndex = family;
		VkCommandPool pool = VK_NULL_HANDLE;
		REQUIRE(D<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device, &pci, nullptr, &pool) == VK_SUCCESS);
		VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		ai.commandPool = pool;
		ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ai.commandBufferCount = 1;
		VkCommandBuffer cb = VK_NULL_HANDLE;
		REQUIRE(D<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device, &ai, &cb) == VK_SUCCESS);
		VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		D<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &bi);
		VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		auto barrier = D<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
		barrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
		VkBufferImageCopy region{};
		region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.imageExtent = { kSize, kSize, 1 };
		D<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(cb, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		std::swap(b.oldLayout, b.newLayout);
		b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
		D<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb);
		VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cb;
		CHECK(D<PFN_vkQueueSubmit>("vkQueueSubmit")(queue, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS);
		D<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(queue);
		D<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device, pool, nullptr);
		D<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device, buffer, nullptr);
		D<PFN_vkFreeMemory>("vkFreeMemory")(device, mem, nullptr);
	}
};

// A hidden window with an OpenGL context current on this thread
struct GlContext {
	HWND window = nullptr;
	HDC dc = nullptr;
	HGLRC rc = nullptr;

	GlContext()
	{
		WNDCLASSA wc{};
		wc.lpfnWndProc = DefWindowProcA;
		wc.hInstance = GetModuleHandleA(nullptr);
		wc.lpszClassName = "oc_runtime_smoke_gl";
		RegisterClassA(&wc);
		window = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
		if (!window)
			return;
		dc = GetDC(window);
		PIXELFORMATDESCRIPTOR pfd{ sizeof(pfd), 1 };
		pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		int format = ChoosePixelFormat(dc, &pfd);
		if (!format || !SetPixelFormat(dc, format, &pfd))
			return;
		rc = wglCreateContext(dc);
		if (rc && !wglMakeCurrent(dc, rc)) {
			wglDeleteContext(rc);
			rc = nullptr;
		}
	}
	~GlContext()
	{
		if (rc) {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(rc);
		}
		if (dc)
			ReleaseDC(window, dc);
		if (window)
			DestroyWindow(window);
	}
	// A driver's OpenGL 4.3 or later (the readback of stored texels needs glCopyImageSubData); Windows' own 1.1 isn't
	bool Usable() const { return rc && wglGetProcAddress("glCopyImageSubData") != nullptr; }
};

bool OpenGLUsable()
{
	GlContext ctx;
	return ctx.Usable();
}

struct OpenGLGpu : Gpu {
	std::unique_ptr<GlContext> ctx;
	XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };

	const char* Extension() override { return XR_KHR_OPENGL_ENABLE_EXTENSION_NAME; }
	const char* BindingName() override { return "opengl"; }
	int64_t Format() override { return 0x8C43; } // GL_SRGB8_ALPHA8
	bool BottomUp() override { return true; }
	const void* Binding() override { return &binding; }

	bool Make(XrInstance instance, XrSystemId system) override
	{
		auto getReqs = XrFn<PFN_xrGetOpenGLGraphicsRequirementsKHR>(instance, "xrGetOpenGLGraphicsRequirementsKHR");
		XrGraphicsRequirementsOpenGLKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
		if (!getReqs || XR_FAILED(getReqs(instance, system, &reqs)))
			return false;
		ctx = std::make_unique<GlContext>();
		if (!ctx->Usable())
			return false;
		binding.hDC = ctx->dc;
		binding.hGLRC = ctx->rc;
		return true;
	}

	void Upload(XrSwapchain sc, uint32_t index, const std::vector<uint8_t>& rgba) override
	{
		uint32_t count = 0;
		xrEnumerateSwapchainImages(sc, 0, &count, nullptr);
		std::vector<XrSwapchainImageOpenGLKHR> images(count, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
		REQUIRE(XR_SUCCEEDED(xrEnumerateSwapchainImages(sc, count, &count, (XrSwapchainImageBaseHeader*)images.data())));
		glBindTexture(GL_TEXTURE_2D, images[index].image);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		glBindTexture(GL_TEXTURE_2D, 0);
		glFinish();
		CHECK(glGetError() == GL_NO_ERROR);
	}
};

// An instance with one extension, and its system
struct XrInst {
	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	explicit XrInst(const char* extension)
	{
		const char* exts[] = { extension };
		XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
		std::snprintf(ici.applicationInfo.applicationName, sizeof(ici.applicationInfo.applicationName), "runtime_smoke");
		ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
		ici.enabledExtensionCount = 1;
		ici.enabledExtensionNames = exts;
		REQUIRE(XR_SUCCEEDED(xrCreateInstance(&ici, &instance)));
		XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
		sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		XR_OK(xrGetSystem(instance, &sgi, &system));
	}
	~XrInst() { xrDestroyInstance(instance); }
	XrResult Session(const void* binding, XrSession* out)
	{
		XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
		sci.next = binding;
		sci.systemId = system;
		return xrCreateSession(instance, &sci, out);
	}
};

// One projection frame of the two-tone image (top red, bottom mid-grey) on the API, read back by the runtime
void TwoToneFrame(Gpu& gpu)
{
	REQUIRE(*DumpPath());
	std::remove(DumpPath());
	{
		XrInst xr(gpu.Extension());
		bool made = gpu.Make(xr.instance, xr.system);
		CHECK(made);
		XrSession session = XR_NULL_HANDLE;
		if (made)
			XR_OK(xr.Session(gpu.Binding(), &session));
		if (session) {
			XrSessionBeginInfo sbi{ XR_TYPE_SESSION_BEGIN_INFO };
			sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			XR_OK(xrBeginSession(session, &sbi));
			XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
			rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
			rsci.poseInReferenceSpace.orientation.w = 1;
			XrSpace stage = XR_NULL_HANDLE;
			XR_OK(xrCreateReferenceSpace(session, &rsci, &stage));

			uint32_t formatCount = 0;
			XR_OK(xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr));
			std::vector<int64_t> formats(formatCount);
			XR_OK(xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()));
			CHECK(std::find(formats.begin(), formats.end(), gpu.Format()) != formats.end());

			XrSwapchainCreateInfo swci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			swci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
			swci.format = gpu.Format();
			swci.sampleCount = 1;
			swci.width = swci.height = kSize;
			swci.faceCount = swci.arraySize = swci.mipCount = 1;
			XrSwapchain sc = XR_NULL_HANDLE;
			XR_OK(xrCreateSwapchain(session, &swci, &sc));
			if (sc) {
				XrFrameState fs{ XR_TYPE_FRAME_STATE };
				XR_OK(xrWaitFrame(session, nullptr, &fs));
				XR_OK(xrBeginFrame(session, nullptr));
				uint32_t index = 0;
				XR_OK(xrAcquireSwapchainImage(sc, nullptr, &index));
				XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
				wi.timeout = XR_INFINITE_DURATION;
				XR_OK(xrWaitSwapchainImage(sc, &wi));
				gpu.Upload(sc, index, TwoTone(gpu.BottomUp()));
				XR_OK(xrReleaseSwapchainImage(sc, nullptr));
				XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
				for (int e = 0; e < 2; e++) {
					pv[e].pose = { { 0, 0, 0, 1 }, { e == 0 ? -0.03f : 0.03f, 1.7f, 0 } };
					pv[e].fov = { -0.7f, 0.7f, 0.7f, -0.7f };
					pv[e].subImage.swapchain = sc;
					pv[e].subImage.imageRect.extent = { (int32_t)kSize, (int32_t)kSize };
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
				xrDestroySwapchain(sc);
			}
			xrDestroySpace(stage);
			if (XR_SUCCEEDED(xrRequestExitSession(session)))
				xrEndSession(session);
			xrDestroySession(session);
		}
	}

	Json::Value dump = ReadDump();
	CHECK(dump["session"]["binding"].asString() == std::string(gpu.BindingName()));
	REQUIRE(dump["layers"].size() == 1);
	const Json::Value& v0 = dump["layers"][0]["views"][0];
	CHECK(v0["format"].asInt64() == gpu.Format());
	CHECK(v0["samples"]["tl"].asString() == "ff0000ff");
	CHECK(v0["samples"]["tr"].asString() == "ff0000ff");
	// The stored sRGB bytes, unconverted
	CHECK(v0["samples"]["bl"].asString() == "808080ff");
	CHECK(v0["samples"]["br"].asString() == "808080ff");
	CHECK(v0["samples"]["c"].asString() == "808080ff");
	CHECK(dump["layers"][0]["invalid"].size() == 0);
}

} // namespace

// The *_red_frame tests: one frame on each API, its top half red and its bottom half mid-grey
TEST_CASE("d3d11_red_frame")
{
	D3D11Gpu gpu;
	TwoToneFrame(gpu);
}

TEST_CASE("d3d12_red_frame")
{
	D3D12Gpu gpu;
	TwoToneFrame(gpu);
}

TEST_CASE("vulkan_red_frame" * doctest::skip(!VulkanUsable()))
{
	VulkanGpu gpu;
	TwoToneFrame(gpu);
}

TEST_CASE("vulkan2_red_frame" * doctest::skip(!VulkanUsable()))
{
	VulkanGpu gpu(true); // XR_KHR_vulkan_enable2 alone
	TwoToneFrame(gpu);
}

TEST_CASE("gl_red_frame" * doctest::skip(!OpenGLUsable()))
{
	OpenGLGpu gpu;
	TwoToneFrame(gpu);
}

// The binding checks Monado makes: the physical device xrGetVulkanGraphicsDeviceKHR named, valid queue indices
TEST_CASE("vulkan_session_checks" * doctest::skip(!VulkanUsable()))
{
	XrInst xr(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
	VulkanGpu gpu;
	REQUIRE(gpu.Make(xr.instance, xr.system));
	// The same GPU through another instance: another handle, which the runtime didn't name
	VkInstance other = MakeVkInstance(gpu.gipa);
	REQUIRE(other);
	uint32_t n = 1;
	VkPhysicalDevice otherPhysical = VK_NULL_HANDLE;
	((PFN_vkEnumeratePhysicalDevices)gpu.gipa(other, "vkEnumeratePhysicalDevices"))(other, &n, &otherPhysical);
	XrSession session = XR_NULL_HANDLE;
	XrGraphicsBindingVulkanKHR b = gpu.binding;
	b.physicalDevice = otherPhysical;
	CHECK(xr.Session(&b, &session) == XR_ERROR_VALIDATION_FAILURE);
	b = gpu.binding;
	b.instance = other; // never asked about
	b.physicalDevice = otherPhysical;
	CHECK(xr.Session(&b, &session) == XR_ERROR_VALIDATION_FAILURE);
	b = gpu.binding;
	b.queueFamilyIndex = 999;
	CHECK(xr.Session(&b, &session) == XR_ERROR_GRAPHICS_DEVICE_INVALID);
	b = gpu.binding;
	b.queueIndex = 99;
	CHECK(xr.Session(&b, &session) == XR_ERROR_GRAPHICS_DEVICE_INVALID);
	CHECK(XR_SUCCEEDED(xr.Session(&gpu.binding, &session)));
	if (session)
		xrDestroySession(session);
	((PFN_vkDestroyInstance)gpu.gipa(other, "vkDestroyInstance"))(other, nullptr);
}

// A D3D device on another adapter than the requirements' LUID: XR_ERROR_GRAPHICS_DEVICE_INVALID
TEST_CASE("d3d_session_checks")
{
	{
		XrInst xr(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
		auto getReqs = XrFn<PFN_xrGetD3D11GraphicsRequirementsKHR>(xr.instance, "xrGetD3D11GraphicsRequirementsKHR");
		XrGraphicsRequirementsD3D11KHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
		REQUIRE(XR_SUCCEEDED(getReqs(xr.instance, xr.system, &reqs)));
		ID3D11Device* warp = nullptr;
		D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &warp, nullptr, nullptr);
		REQUIRE(warp);
		IDXGIDevice* dxgi = nullptr;
		IDXGIAdapter* adapter = nullptr;
		DXGI_ADAPTER_DESC desc{};
		warp->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi);
		dxgi->GetAdapter(&adapter);
		adapter->GetDesc(&desc);
		adapter->Release();
		dxgi->Release();
		XrGraphicsBindingD3D11KHR b{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
		b.device = warp;
		XrSession session = XR_NULL_HANDLE;
		if (std::memcmp(&desc.AdapterLuid, &reqs.adapterLuid, sizeof(LUID)) != 0)
			CHECK(xr.Session(&b, &session) == XR_ERROR_GRAPHICS_DEVICE_INVALID);
		else
			MESSAGE("WARP is the requirements' adapter here: nothing to refuse");
		warp->Release();
	}
	{
		XrInst xr(XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
		D3D12Gpu gpu;
		gpu.warp = true;
		REQUIRE(gpu.Make(xr.instance, xr.system));
		auto getReqs = XrFn<PFN_xrGetD3D12GraphicsRequirementsKHR>(xr.instance, "xrGetD3D12GraphicsRequirementsKHR");
		XrGraphicsRequirementsD3D12KHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR };
		REQUIRE(XR_SUCCEEDED(getReqs(xr.instance, xr.system, &reqs)));
		LUID luid = gpu.device->GetAdapterLuid();
		XrSession session = XR_NULL_HANDLE;
		if (std::memcmp(&luid, &reqs.adapterLuid, sizeof(LUID)) != 0)
			CHECK(xr.Session(gpu.Binding(), &session) == XR_ERROR_GRAPHICS_DEVICE_INVALID);
		else
			MESSAGE("WARP is the requirements' adapter here: nothing to refuse");
	}
}
