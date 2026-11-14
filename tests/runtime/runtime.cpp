// oc_test_runtime: a stand-in OpenXR runtime for OpenComposite's tests. One instance, one session at a time, a
// simulated HMD standing still at (0, 1.7, 0), D3D11 swapchains, scripted failures and events (script.h).
//
// OpenComposite calls (grep -rhoE "\bxr[A-Z][A-Za-z0-9]+\b" DrvOpenXR OpenOVR OCOVR):
//   xrAcquireSwapchainImage xrApplyHapticFeedback xrAttachSessionActionSets xrBeginFrame xrBeginSession
//   xrCreateAction xrCreateActionSet xrCreateActionSpace xrCreateDebugUtilsMessengerEXT xrCreateHandTrackerEXT
//   xrCreateInstance xrCreateReferenceSpace xrCreateSession xrCreateSwapchain xrCreateXDevListMNDX
//   xrCreateXDevSpaceMNDX xrDestroyActionSet xrDestroyDebugUtilsMessengerEXT xrDestroyHandTrackerEXT
//   xrDestroyInstance xrDestroySession xrDestroySpace xrDestroySwapchain xrDestroyXDevListMNDX xrEndFrame
//   xrEndSession xrEnumerateApiLayerProperties xrEnumerateBoundSourcesForAction
//   xrEnumerateInstanceExtensionProperties xrEnumerateSwapchainFormats xrEnumerateSwapchainImages
//   xrEnumerateViewConfigurationViews xrEnumerateXDevsMNDX xrGetActionStateBoolean xrGetActionStateFloat
//   xrGetActionStateVector2f xrGetCurrentInteractionProfile xrGetD3D11GraphicsRequirementsKHR
//   xrGetD3D12GraphicsRequirementsKHR xrGetInstanceProcAddr xrGetOpenGLESGraphicsRequirementsKHR
//   xrGetOpenGLGraphicsRequirementsKHR xrGetReferenceSpaceBoundsRect xrGetSystem xrGetSystemProperties
//   xrGetVisibilityMaskKHR xrGetVulkanDeviceExtensionsKHR xrGetVulkanGraphicsDeviceKHR
//   xrGetVulkanGraphicsRequirementsKHR xrGetVulkanInstanceExtensionsKHR xrGetXDevListGenerationNumberMNDX
//   xrGetXDevPropertiesMNDX xrLocateHandJointsEXT xrLocateSpace xrLocateViews xrPathToString xrPollEvent
//   xrReleaseSwapchainImage xrRequestExitSession xrResultToString xrStringToPath
//   xrSuggestInteractionProfileBindings xrSyncActions xrWaitFrame xrWaitSwapchainImage
// Anything not handed out by xrGetInstanceProcAddr is XR_ERROR_FUNCTION_UNSUPPORTED.

#include "runtime_internal.h"

#include "probe.h"

#include <openxr/openxr_reflection.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

RtState& St()
{
	static RtState state;
	return state;
}

XrPath RtPath(const std::string& str)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto it = St().pathIds.find(str);
	if (it != St().pathIds.end())
		return it->second;
	XrPath id = St().paths.size();
	St().paths.push_back(str);
	St().pathIds[str] = id;
	return id;
}

std::string RtPathString(XrPath path)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	if (path == XR_NULL_PATH || path >= St().paths.size())
		return "";
	return St().paths[(size_t)path];
}

void QueueEvent(const XrEventDataBuffer& ev)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	St().events.push_back(ev);
}

void QueueState(RtSession* s, XrSessionState state)
{
	s->state = state;
	XrEventDataBuffer buf{ XR_TYPE_EVENT_DATA_BUFFER };
	auto* ev = (XrEventDataSessionStateChanged*)&buf;
	ev->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
	ev->session = (XrSession)s;
	ev->state = state;
	ev->time = St().time;
	QueueEvent(buf);
}

bool RtExtensionEnabled(const char* name)
{
	return St().enabledExtensions.count(name) != 0;
}

namespace {

XrInstance kInstance = (XrInstance)(uintptr_t)0x1001;
const XrSystemId kSystem = 1;

// The frame period: the session's refresh rate (XR_FB_display_refresh_rate can change it), else the script's
XrTime Period()
{
	float hz = St().session && St().session->refreshRate > 0 ? St().session->refreshRate : GetScript().displayHz;
	return (XrTime)(1e9 / hz);
}

XrSessionState StateFromName(const std::string& n)
{
#define OC_STATE_CASE(name, value)               \
	if (n == std::string(#name).substr(17)) \
		return name;
	XR_LIST_ENUM_XrSessionState(OC_STATE_CASE);
#undef OC_STATE_CASE
	return XR_SESSION_STATE_UNKNOWN;
}

XrPosef SpaceInStage(const RtSpace* sp)
{
	return RtSpaceInStage(sp);
}

uint64_t FramesWaited()
{
	return St().session ? St().session->framesWaited : 0;
}

void CountFrameOrderError()
{
	Dump& d = GetDump();
	std::lock_guard<std::mutex> dg(d.lock);
	Json::Value& n = d.root["frame_order_errors"];
	n = n.asInt() + 1;
}

// The dump's record of the app breaking the swapchain call order, and of images still acquired at destruction
static void NoteSwapchainMisuse(const char* what)
{
	Dump& d = GetDump();
	std::lock_guard<std::mutex> dg(d.lock);
	Json::Value& n = d.root["swapchain_call_order_errors"][what];
	n = n.asInt() + 1;
}

static void NoteSwapchainDestroyed(const RtSwapchain* sc)
{
	Dump& d = GetDump();
	std::lock_guard<std::mutex> dg(d.lock);
	Json::Value& n = d.root["images_left_acquired"];
	n = n.asInt() + sc->acquired;
}

#define FAIL_POINT(name)                       \
	do {                                       \
		XrResult injected;                     \
		if (ScriptFails(name, &injected))      \
			return injected;                   \
	} while (0)

template <typename T>
XrResult TwoCall(uint32_t capacity, uint32_t* count, T* out, const std::vector<T>& values)
{
	*count = (uint32_t)values.size();
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < values.size())
		return XR_ERROR_SIZE_INSUFFICIENT;
	for (size_t i = 0; i < values.size(); i++)
		out[i] = values[i];
	return XR_SUCCESS;
}

// --- instance ---

XrResult XRAPI_CALL EnumerateApiLayerProperties(uint32_t, uint32_t* count, XrApiLayerProperties*)
{
	*count = 0;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL EnumerateInstanceExtensionProperties(const char* layer, uint32_t capacity, uint32_t* count, XrExtensionProperties* props)
{
	if (layer)
		return XR_ERROR_API_LAYER_NOT_PRESENT;
	std::vector<XrExtensionProperties> list;
	for (const std::string& name : GetScript().extensions) {
		XrExtensionProperties p{ XR_TYPE_EXTENSION_PROPERTIES };
		std::snprintf(p.extensionName, sizeof(p.extensionName), "%s", name.c_str());
		p.extensionVersion = 1;
		list.push_back(p);
	}
	return TwoCall(capacity, count, props, list);
}

XrResult XRAPI_CALL CreateInstance(const XrInstanceCreateInfo* info, XrInstance* instance)
{
	FAIL_POINT("xrCreateInstance");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	if (St().instanceAlive)
		return XR_ERROR_LIMIT_REACHED;
	std::set<std::string> enabled;
	for (uint32_t i = 0; i < info->enabledExtensionCount; i++) {
		std::string name = info->enabledExtensionNames[i];
		if (!GetScript().extensions.count(name))
			return XR_ERROR_EXTENSION_NOT_PRESENT;
		enabled.insert(name);
	}
	St().instanceAlive = true;
	St().d3d11RequirementsQueried = false;
	St().enabledExtensions = enabled;
	{
		Dump& d = GetDump();
		std::lock_guard<std::mutex> dg(d.lock);
		d.root = Json::Value(Json::objectValue);
		d.root["instance"]["extensions"] = Json::Value(Json::arrayValue);
		for (const std::string& e : enabled)
			d.root["instance"]["extensions"].append(e);
		d.root["instance"]["application"] = info->applicationInfo.applicationName;
		d.root["frames_ended"] = 0;
		d.root["layers"] = Json::Value(Json::arrayValue);
		d.root["frame_order_errors"] = 0;
		d.root["refresh_rate_requests"] = 0;
	}
	*instance = kInstance;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroyInstance(XrInstance)
{
	{
		std::lock_guard<std::recursive_mutex> g(St().lock);
		for (auto& [p, sc] : St().swapchains) {
			NoteSwapchainDestroyed(sc.get());
			RtD3D11DestroySwapchain(sc.get());
		}
		St().swapchains.clear();
		St().spaces.clear();
		if (St().session && St().session->device)
			St().session->device->Release();
		St().session.reset();
		St().events.clear();
		St().instanceAlive = false;
		Dump& d = GetDump();
		std::lock_guard<std::mutex> dg(d.lock);
		RtActionsDump(d.root);
		RtActionsReset();
	}
	GetDump().Write();
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetInstanceProperties(XrInstance, XrInstanceProperties* props)
{
	props->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);
	std::snprintf(props->runtimeName, sizeof(props->runtimeName), "OpenComposite test runtime");
	return XR_SUCCESS;
}

XrResult XRAPI_CALL PollEvent(XrInstance, XrEventDataBuffer* out)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	if (St().events.empty())
		return XR_EVENT_UNAVAILABLE;
	*out = St().events.front();
	St().events.pop_front();
	return XR_SUCCESS;
}

XrResult XRAPI_CALL ResultToString(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE])
{
	switch (value) {
#define OC_RESULT_STR(name, v) \
	case name:                 \
		std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "%s", #name); \
		return XR_SUCCESS;
		XR_LIST_ENUM_XrResult(OC_RESULT_STR);
#undef OC_RESULT_STR
	default:
		std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_UNKNOWN_RESULT_%d", (int)value);
		return XR_SUCCESS;
	}
}

XrResult XRAPI_CALL StructureTypeToString(XrInstance, XrStructureType value, char buffer[XR_MAX_STRUCTURE_NAME_SIZE])
{
	switch (value) {
#define OC_TYPE_STR(name, v) \
	case name:               \
		std::snprintf(buffer, XR_MAX_STRUCTURE_NAME_SIZE, "%s", #name); \
		return XR_SUCCESS;
		XR_LIST_ENUM_XrStructureType(OC_TYPE_STR);
#undef OC_TYPE_STR
	default:
		std::snprintf(buffer, XR_MAX_STRUCTURE_NAME_SIZE, "XR_UNKNOWN_STRUCTURE_TYPE_%d", (int)value);
		return XR_SUCCESS;
	}
}

XrResult XRAPI_CALL StringToPath(XrInstance, const char* str, XrPath* path)
{
	if (!str || str[0] != '/')
		return XR_ERROR_PATH_FORMAT_INVALID;
	*path = RtPath(str);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL PathToString(XrInstance, XrPath path, uint32_t capacity, uint32_t* count, char* buffer)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	if (path == XR_NULL_PATH || path >= St().paths.size())
		return XR_ERROR_PATH_INVALID;
	const std::string& s = St().paths[(size_t)path];
	*count = (uint32_t)s.size() + 1;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < *count)
		return XR_ERROR_SIZE_INSUFFICIENT;
	std::memcpy(buffer, s.c_str(), *count);
	return XR_SUCCESS;
}

// --- system ---

XrResult XRAPI_CALL GetSystem(XrInstance, const XrSystemGetInfo* info, XrSystemId* system)
{
	FAIL_POINT("xrGetSystem");
	if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
		return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
	*system = kSystem;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* props)
{
	props->systemId = kSystem;
	props->vendorId = 0x4F43; // "OC"
	std::snprintf(props->systemName, sizeof(props->systemName), "%s", GetScript().systemName.c_str());
	props->graphicsProperties.maxLayerCount = 16;
	props->graphicsProperties.maxSwapchainImageWidth = 8192;
	props->graphicsProperties.maxSwapchainImageHeight = 8192;
	props->trackingProperties.orientationTracking = XR_TRUE;
	props->trackingProperties.positionTracking = XR_TRUE;
	RtExtensionsSystemProperties(props);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL EnumerateEnvironmentBlendModes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity, uint32_t* count, XrEnvironmentBlendMode* modes)
{
	return TwoCall(capacity, count, modes, std::vector<XrEnvironmentBlendMode>{ XR_ENVIRONMENT_BLEND_MODE_OPAQUE });
}

XrResult XRAPI_CALL EnumerateViewConfigurations(XrInstance, XrSystemId, uint32_t capacity, uint32_t* count, XrViewConfigurationType* types)
{
	return TwoCall(capacity, count, types, std::vector<XrViewConfigurationType>{ XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO });
}

XrResult XRAPI_CALL GetViewConfigurationProperties(XrInstance, XrSystemId, XrViewConfigurationType type, XrViewConfigurationProperties* props)
{
	if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	props->viewConfigurationType = type;
	props->fovMutable = XR_TRUE;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL EnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType type, uint32_t capacity, uint32_t* count, XrViewConfigurationView* views)
{
	if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	*count = 2;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < 2)
		return XR_ERROR_SIZE_INSUFFICIENT;
	for (int i = 0; i < 2; i++) {
		views[i].recommendedImageRectWidth = GetScript().viewWidth;
		views[i].recommendedImageRectHeight = GetScript().viewHeight;
		views[i].maxImageRectWidth = 8192;
		views[i].maxImageRectHeight = 8192;
		views[i].recommendedSwapchainSampleCount = 1;
		views[i].maxSwapchainSampleCount = 4;
	}
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetD3D11GraphicsRequirements(XrInstance, XrSystemId, XrGraphicsRequirementsD3D11KHR* reqs)
{
	FAIL_POINT("xrGetD3D11GraphicsRequirementsKHR");
	{
		std::lock_guard<std::recursive_mutex> g(St().lock);
		St().d3d11RequirementsQueried = true;
	}
	return RtD3D11Requirements(reqs);
}

// --- session ---

XrResult XRAPI_CALL CreateSession(XrInstance, const XrSessionCreateInfo* info, XrSession* out)
{
	FAIL_POINT("xrCreateSession");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	if (St().session)
		return XR_ERROR_LIMIT_REACHED;
	auto session = std::make_unique<RtSession>();
	session->refreshRate = GetScript().refreshRates.empty() ? GetScript().displayHz : GetScript().refreshRates[0];
	std::string binding = "none";
	for (auto* next = (const XrBaseInStructure*)info->next; next; next = next->next) {
		if (next->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
			session->device = ((const XrGraphicsBindingD3D11KHR*)next)->device;
			binding = "d3d11";
		}
	}
	if (binding == "d3d11" && !St().d3d11RequirementsQueried)
		return XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING;
	if (!RtExtensionsSessionInfo(info, session.get()))
		return XR_ERROR_VALIDATION_FAILURE;
	if (session->headless)
		binding = "headless";
	if (!session->device && !session->headless)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	if (session->device)
		session->device->AddRef();
	{
		Dump& d = GetDump();
		std::lock_guard<std::mutex> dg(d.lock);
		d.root["session"]["binding"] = binding;
		d.root["session"]["overlay"] = session->overlay;
	}
	RtSession* raw = session.get();
	St().session = std::move(session);
	QueueState(raw, XR_SESSION_STATE_IDLE);
	QueueState(raw, XR_SESSION_STATE_READY);
	*out = (XrSession)raw;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroySession(XrSession session)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s || (XrSession)s != session)
		return XR_ERROR_HANDLE_INVALID;
	for (auto& [p, sc] : St().swapchains) {
		NoteSwapchainDestroyed(sc.get());
		RtD3D11DestroySwapchain(sc.get());
	}
	St().swapchains.clear();
	St().spaces.clear();
	if (s->device)
		s->device->Release();
	St().session.reset();
	return XR_SUCCESS;
}

XrResult XRAPI_CALL BeginSession(XrSession, const XrSessionBeginInfo* info)
{
	FAIL_POINT("xrBeginSession");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (s->running)
		return XR_ERROR_SESSION_RUNNING;
	if (info->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	s->running = true;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL EndSession(XrSession)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	if (s->state != XR_SESSION_STATE_STOPPING)
		return XR_ERROR_SESSION_NOT_STOPPING;
	s->running = false;
	QueueState(s, XR_SESSION_STATE_IDLE);
	if (s->exitRequested)
		QueueState(s, XR_SESSION_STATE_EXITING);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL RequestExitSession(XrSession)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s || !s->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	s->exitRequested = true;
	QueueState(s, XR_SESSION_STATE_STOPPING);
	return XR_SUCCESS;
}

// --- spaces ---

XrResult XRAPI_CALL EnumerateReferenceSpaces(XrSession, uint32_t capacity, uint32_t* count, XrReferenceSpaceType* types)
{
	return TwoCall(capacity, count, types,
	    std::vector<XrReferenceSpaceType>{ XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL, XR_REFERENCE_SPACE_TYPE_STAGE });
}

XrResult XRAPI_CALL CreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo* info, XrSpace* out)
{
	switch (info->referenceSpaceType) {
	case XR_REFERENCE_SPACE_TYPE_VIEW:
	case XR_REFERENCE_SPACE_TYPE_LOCAL:
	case XR_REFERENCE_SPACE_TYPE_STAGE:
		break;
	default:
		return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
	}
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto sp = std::make_unique<RtSpace>();
	sp->type = info->referenceSpaceType;
	sp->offset = info->poseInReferenceSpace;
	RtSpace* raw = sp.get();
	St().spaces[raw] = std::move(sp);
	*out = (XrSpace)raw;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroySpace(XrSpace space)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	St().spaces.erase((RtSpace*)space);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL LocateSpace(XrSpace space, XrSpace base, XrTime, XrSpaceLocation* loc)
{
	FAIL_POINT("xrLocateSpace");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto a = St().spaces.find((RtSpace*)space), b = St().spaces.find((RtSpace*)base);
	if (a == St().spaces.end() || b == St().spaces.end())
		return XR_ERROR_HANDLE_INVALID;
	ScriptPose motion;
	XrPosef baseInStage = SpaceInStage(b->second.get());
	loc->pose = Compose(Inverse(baseInStage), RtSpaceInStage(a->second.get(), &motion));
	loc->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT
	    | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	// Velocities: the script's, in STAGE, turned into the base space (reference spaces stand still)
	for (auto* n = (XrBaseOutStructure*)loc->next; n; n = n->next) {
		if (n->type != XR_TYPE_SPACE_VELOCITY)
			continue;
		auto* v = (XrSpaceVelocity*)n;
		XrQuaternionf toBase = QConj(baseInStage.orientation);
		v->linearVelocity = QRotate(toBase, motion.linear);
		v->angularVelocity = QRotate(toBase, motion.angular);
		v->velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
	}
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetReferenceSpaceBoundsRect(XrSession, XrReferenceSpaceType type, XrExtent2Df* bounds)
{
	if (type != XR_REFERENCE_SPACE_TYPE_STAGE) {
		*bounds = { 0, 0 };
		return XR_SPACE_BOUNDS_UNAVAILABLE;
	}
	*bounds = { GetScript().stageWidth, GetScript().stageDepth };
	return XR_SUCCESS;
}

XrResult XRAPI_CALL LocateViews(XrSession, const XrViewLocateInfo* info, XrViewState* state, uint32_t capacity, uint32_t* count, XrView* views)
{
	FAIL_POINT("xrLocateViews");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto b = St().spaces.find((RtSpace*)info->space);
	if (b == St().spaces.end())
		return XR_ERROR_HANDLE_INVALID;
	*count = 2;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < 2)
		return XR_ERROR_SIZE_INSUFFICIENT;
	XrPosef headInBase = Compose(Inverse(SpaceInStage(b->second.get())), kHead);
	float half = GetScript().fovDeg * 3.14159265f / 180.0f;
	float ipd = ScriptIpdAt(FramesWaited());
	for (int i = 0; i < 2; i++) {
		XrPosef eye{ { 0, 0, 0, 1 }, { (i == 0 ? -0.5f : 0.5f) * ipd, 0, 0 } };
		views[i].pose = Compose(headInBase, eye);
		views[i].fov = { -half, half, half, -half };
	}
	state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT
	    | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
	return XR_SUCCESS;
}

// --- swapchains ---

// What xrEnumerateSwapchainFormats lists without the script's swapchain_formats
const std::vector<int64_t>& DefaultFormats()
{
	static const std::vector<int64_t> d3d = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB };
	return d3d;
}

XrResult XRAPI_CALL EnumerateSwapchainFormats(XrSession, uint32_t capacity, uint32_t* count, int64_t* formats)
{
	const std::vector<int64_t>& scripted = GetScript().swapchainFormats;
	return TwoCall(capacity, count, formats, scripted.empty() ? DefaultFormats() : scripted);
}

XrResult XRAPI_CALL CreateSwapchain(XrSession, const XrSwapchainCreateInfo* info, XrSwapchain* out)
{
	FAIL_POINT("xrCreateSwapchain");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	// Scripted formats are the only ones; without them, any the API's code can make
	const std::vector<int64_t>& scripted = GetScript().swapchainFormats;
	if (!scripted.empty() && std::find(scripted.begin(), scripted.end(), info->format) == scripted.end())
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	auto sc = std::make_unique<RtSwapchain>();
	XrResult r = RtD3D11CreateSwapchain(s, info, sc.get());
	if (XR_FAILED(r))
		return r;
	RtSwapchain* raw = sc.get();
	St().swapchains[raw] = std::move(sc);
	*out = (XrSwapchain)raw;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroySwapchain(XrSwapchain swapchain)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto it = St().swapchains.find((RtSwapchain*)swapchain);
	if (it == St().swapchains.end())
		return XR_ERROR_HANDLE_INVALID;
	NoteSwapchainDestroyed(it->second.get());
	RtD3D11DestroySwapchain(it->second.get());
	St().swapchains.erase(it);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL EnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto it = St().swapchains.find((RtSwapchain*)swapchain);
	if (it == St().swapchains.end())
		return XR_ERROR_HANDLE_INVALID;
	RtSwapchain* sc = it->second.get();
	*count = (uint32_t)sc->textures.size();
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < *count)
		return XR_ERROR_SIZE_INSUFFICIENT;
	auto* out = (XrSwapchainImageD3D11KHR*)images;
	for (uint32_t i = 0; i < *count; i++)
		out[i].texture = sc->textures[i];
	return XR_SUCCESS;
}

XrResult XRAPI_CALL AcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index)
{
	FAIL_POINT("xrAcquireSwapchainImage");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto it = St().swapchains.find((RtSwapchain*)swapchain);
	if (it == St().swapchains.end())
		return XR_ERROR_HANDLE_INVALID;
	RtSwapchain* sc = it->second.get();
	if (sc->acquired >= (int)sc->textures.size()) {
		NoteSwapchainMisuse("acquire");
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	*index = sc->next;
	sc->acquiredOrder.push_back(sc->next);
	sc->next = (sc->next + 1) % (uint32_t)sc->textures.size();
	sc->acquired++;
	sc->unwaited++;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL WaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo*)
{
	// An injected XR_TIMEOUT_EXPIRED leaves the image unwaited, as a real runtime's would
	FAIL_POINT("xrWaitSwapchainImage");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto it = St().swapchains.find((RtSwapchain*)swapchain);
	if (it == St().swapchains.end())
		return XR_ERROR_HANDLE_INVALID;
	if (it->second->unwaited <= 0) {
		NoteSwapchainMisuse("wait");
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	it->second->unwaited--;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL ReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*)
{
	FAIL_POINT("xrReleaseSwapchainImage");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto it = St().swapchains.find((RtSwapchain*)swapchain);
	if (it == St().swapchains.end())
		return XR_ERROR_HANDLE_INVALID;
	// Only a waited image can be released (the oldest acquired one)
	if (it->second->acquired - it->second->unwaited <= 0) {
		NoteSwapchainMisuse("release");
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	it->second->acquired--;
	if (!it->second->acquiredOrder.empty()) {
		it->second->lastReleased = (int)it->second->acquiredOrder.front();
		it->second->acquiredOrder.pop_front();
	}
	return XR_SUCCESS;
}

// --- frames ---

XrResult XRAPI_CALL WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* fs)
{
	FAIL_POINT("xrWaitFrame");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	// A second wait before the first one's frame began
	if (s->framesWaited > s->framesBegun)
		CountFrameOrderError();
	s->framesWaited++;
	if (s->framesWaited == 1)
		QueueState(s, XR_SESSION_STATE_SYNCHRONIZED);
	else if (s->framesWaited == 2)
		QueueState(s, XR_SESSION_STATE_VISIBLE);
	else if (s->framesWaited == 3)
		QueueState(s, XR_SESSION_STATE_FOCUSED);
	for (const ScriptEvent& ev : GetScript().events) {
		if (ev.frame == s->framesWaited && ev.type == "session_state")
			QueueState(s, StateFromName(ev.state));
	}
	RtActionsOnFrame(s);
	St().time += Period();
	fs->predictedDisplayTime = St().time + Period();
	fs->predictedDisplayPeriod = Period();
	fs->shouldRender = (s->state == XR_SESSION_STATE_VISIBLE || s->state == XR_SESSION_STATE_FOCUSED) ? XR_TRUE : XR_FALSE;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL BeginFrame(XrSession, const XrFrameBeginInfo*)
{
	FAIL_POINT("xrBeginFrame");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	if (s->framesBegun >= s->framesWaited) {
		CountFrameOrderError(); // a begin without a wait
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	bool discarded = s->frameOpen;
	s->frameOpen = true;
	s->framesBegun++;
	return discarded ? XR_FRAME_DISCARDED : XR_SUCCESS;
}

const char* LayerTypeName(XrStructureType type)
{
	switch (type) {
	case XR_TYPE_COMPOSITION_LAYER_PROJECTION:
		return "projection";
	case XR_TYPE_COMPOSITION_LAYER_QUAD:
		return "quad";
	case XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR:
		return "cylinder";
	case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR:
		return "equirect2";
	case XR_TYPE_COMPOSITION_LAYER_CUBE_KHR:
		return "cube";
	default:
		return "unknown";
	}
}

// --- the dump's layer records ---

Json::Value Vec(std::initializer_list<double> values)
{
	Json::Value v(Json::arrayValue);
	for (double x : values)
		v.append(x);
	return v;
}

Json::Value PoseJson(const XrPosef& p)
{
	Json::Value v;
	v["p"] = Vec({ p.position.x, p.position.y, p.position.z });
	v["q"] = Vec({ p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w });
	return v;
}

const char* EyeName(XrEyeVisibility eye)
{
	return eye == XR_EYE_VISIBILITY_LEFT ? "left" : eye == XR_EYE_VISIBILITY_RIGHT ? "right" : "both";
}

void Invalid(std::vector<std::string>& invalid, const std::string& what)
{
	if (std::find(invalid.begin(), invalid.end(), what) == invalid.end())
		invalid.push_back(what);
}

Json::Value InvalidJson(const std::vector<std::string>& invalid)
{
	Json::Value v(Json::arrayValue);
	for (const std::string& s : invalid)
		v.append(s);
	return v;
}

bool Normalized(const XrQuaternionf& q)
{
	float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	return std::fabs(n - 1) < 0.01f;
}

// The space's name, and the spec's checks of a layer's space and orientation (q: none for projections)
void RecordPlacement(Json::Value& entry, XrSpace space, const XrQuaternionf* q, std::vector<std::string>& invalid)
{
	auto it = St().spaces.find((RtSpace*)space);
	if (it == St().spaces.end()) {
		entry["space"] = "other";
		Invalid(invalid, "unknown space");
	} else if (it->second->isAction) {
		entry["space"] = "other";
	} else {
		XrReferenceSpaceType t = it->second->type;
		entry["space"] = t == XR_REFERENCE_SPACE_TYPE_LOCAL ? "local"
		    : t == XR_REFERENCE_SPACE_TYPE_STAGE            ? "stage"
		    : t == XR_REFERENCE_SPACE_TYPE_VIEW             ? "view"
		                                                    : "other";
	}
	if (q && !Normalized(*q))
		Invalid(invalid, "pose not normalized");
}

// rect, array_index, format and (when sample) samples of a sub-image, with the spec's checks of it; prefix names
// the image in the checks ("depth " for a depth sub-image)
void RecordSubImage(RtSession* s, Json::Value& entry, const XrSwapchainSubImage& sub, bool sample,
    std::vector<std::string>& invalid, const std::string& prefix = "")
{
	const XrRect2Di& r = sub.imageRect;
	entry["rect"] = Json::Value(Json::arrayValue);
	for (int32_t v : { r.offset.x, r.offset.y, r.extent.width, r.extent.height })
		entry["rect"].append(v);
	entry["array_index"] = sub.imageArrayIndex;
	auto it = St().swapchains.find((RtSwapchain*)sub.swapchain);
	if (it == St().swapchains.end()) {
		entry["format"] = Json::Value();
		Invalid(invalid, prefix + "unknown swapchain");
		return;
	}
	RtSwapchain* sc = it->second.get();
	entry["format"] = (Json::Int64)sc->info.format;
	if (r.extent.width < 0 || r.extent.height < 0)
		Invalid(invalid, prefix + "negative extent");
	else if (r.extent.width == 0 || r.extent.height == 0)
		Invalid(invalid, prefix + "zero extent");
	if (r.offset.x < 0 || r.offset.y < 0)
		Invalid(invalid, prefix + "negative offset");
	if ((int64_t)r.offset.x + r.extent.width > (int64_t)sc->info.width || (int64_t)r.offset.y + r.extent.height > (int64_t)sc->info.height)
		Invalid(invalid, prefix + "rect outside image");
	uint32_t layers = sc->info.arraySize ? sc->info.arraySize : 1;
	if (sub.imageArrayIndex >= layers)
		Invalid(invalid, prefix + "array index out of range");
	if (sc->lastReleased < 0)
		Invalid(invalid, prefix + "no image released");
	if (!sample || sc->lastReleased < 0)
		return;
	SwapchainImage image{ s, sc, (uint32_t)sc->lastReleased };
	Samples samples;
	if (ProbeSamples(image, r, sub.imageArrayIndex, samples))
		entry["samples"] = SamplesJson(samples);
}

void NeedExtension(const char* extension, const char* what, std::vector<std::string>& invalid)
{
	if (!RtExtensionEnabled(extension))
		Invalid(invalid, std::string(what) + " without " + extension);
}

bool AngleIn(float a, float lo, float hi)
{
	return a >= lo - 1e-5f && a <= hi + 1e-5f;
}

Json::Value RecordProjectionView(RtSession* s, const XrCompositionLayerProjectionView& pv, bool sample)
{
	Json::Value view;
	std::vector<std::string> invalid;
	view["pose"] = PoseJson(pv.pose);
	view["fov"] = Vec({ pv.fov.angleLeft, pv.fov.angleRight, pv.fov.angleUp, pv.fov.angleDown });
	if (!Normalized(pv.pose.orientation))
		Invalid(invalid, "pose not normalized");
	RecordSubImage(s, view, pv.subImage, sample, invalid);
	view["depth"] = Json::Value();
	for (auto* n = (const XrBaseInStructure*)pv.next; n; n = n->next) {
		if (n->type != XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
			continue;
		auto* di = (const XrCompositionLayerDepthInfoKHR*)n;
		Json::Value depth;
		depth["near"] = di->nearZ;
		depth["far"] = di->farZ;
		Json::Value sub;
		RecordSubImage(s, sub, di->subImage, false, invalid, "depth ");
		depth["format"] = sub["format"];
		depth["rect"] = sub["rect"];
		if (!(di->minDepth >= 0 && di->maxDepth <= 1 && di->minDepth <= di->maxDepth))
			Invalid(invalid, "depth range");
		if (di->nearZ == di->farZ)
			Invalid(invalid, "depth near equals far");
		NeedExtension("XR_KHR_composition_layer_depth", "depth", invalid);
		view["depth"] = depth;
	}
	view["invalid"] = InvalidJson(invalid);
	return view;
}

// A layer's dump record: placement, images and pixels, and every check of the spec's that it fails
Json::Value RecordLayer(RtSession* s, const XrCompositionLayerBaseHeader* layer, bool sample)
{
	const float pi = 3.14159265f;
	Json::Value entry;
	std::vector<std::string> invalid;
	if (!layer) {
		entry["type"] = "null";
		Invalid(invalid, "null layer");
		entry["invalid"] = InvalidJson(invalid);
		return entry;
	}
	entry["type"] = LayerTypeName(layer->type);
	switch (layer->type) {
	case XR_TYPE_COMPOSITION_LAYER_PROJECTION: {
		auto* proj = (const XrCompositionLayerProjection*)layer;
		entry["flags"] = (Json::UInt64)proj->layerFlags;
		entry["eye_visibility"] = "both";
		RecordPlacement(entry, proj->space, nullptr, invalid);
		if (proj->viewCount != 2)
			Invalid(invalid, "view count");
		if (proj->viewCount && !proj->views)
			Invalid(invalid, "null views");
		entry["views"] = Json::Value(Json::arrayValue);
		for (uint32_t v = 0; v < proj->viewCount && proj->views; v++) {
			Json::Value view = RecordProjectionView(s, proj->views[v], sample);
			for (const Json::Value& w : view["invalid"])
				Invalid(invalid, w.asString());
			entry["views"].append(view);
		}
		break;
	}
	case XR_TYPE_COMPOSITION_LAYER_QUAD: {
		auto* quad = (const XrCompositionLayerQuad*)layer;
		entry["flags"] = (Json::UInt64)quad->layerFlags;
		entry["eye_visibility"] = EyeName(quad->eyeVisibility);
		RecordPlacement(entry, quad->space, &quad->pose.orientation, invalid);
		entry["pose"] = PoseJson(quad->pose);
		entry["size"] = Vec({ quad->size.width, quad->size.height });
		RecordSubImage(s, entry, quad->subImage, sample, invalid);
		break;
	}
	case XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR: {
		auto* cyl = (const XrCompositionLayerCylinderKHR*)layer;
		entry["flags"] = (Json::UInt64)cyl->layerFlags;
		entry["eye_visibility"] = EyeName(cyl->eyeVisibility);
		RecordPlacement(entry, cyl->space, &cyl->pose.orientation, invalid);
		entry["pose"] = PoseJson(cyl->pose);
		entry["radius"] = cyl->radius;
		entry["angles"] = Vec({ cyl->centralAngle });
		entry["aspect_ratio"] = cyl->aspectRatio;
		if (cyl->radius < 0)
			Invalid(invalid, "negative radius");
		if (!AngleIn(cyl->centralAngle, 0, 2 * pi))
			Invalid(invalid, "central angle out of range");
		if (!(cyl->aspectRatio > 0))
			Invalid(invalid, "aspect ratio not positive");
		NeedExtension("XR_KHR_composition_layer_cylinder", "cylinder", invalid);
		RecordSubImage(s, entry, cyl->subImage, sample, invalid);
		break;
	}
	case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR: {
		auto* eq = (const XrCompositionLayerEquirect2KHR*)layer;
		entry["flags"] = (Json::UInt64)eq->layerFlags;
		entry["eye_visibility"] = EyeName(eq->eyeVisibility);
		RecordPlacement(entry, eq->space, &eq->pose.orientation, invalid);
		entry["pose"] = PoseJson(eq->pose);
		entry["radius"] = eq->radius;
		entry["angles"] = Vec({ eq->centralHorizontalAngle, eq->upperVerticalAngle, eq->lowerVerticalAngle });
		if (eq->radius < 0)
			Invalid(invalid, "negative radius");
		if (!AngleIn(eq->centralHorizontalAngle, 0, 2 * pi))
			Invalid(invalid, "central angle out of range");
		if (!AngleIn(eq->upperVerticalAngle, -pi / 2, pi / 2) || !AngleIn(eq->lowerVerticalAngle, -pi / 2, pi / 2)
		    || eq->lowerVerticalAngle > eq->upperVerticalAngle)
			Invalid(invalid, "vertical angles out of range");
		NeedExtension("XR_KHR_composition_layer_equirect2", "equirect2", invalid);
		RecordSubImage(s, entry, eq->subImage, sample, invalid);
		break;
	}
	case XR_TYPE_COMPOSITION_LAYER_CUBE_KHR: {
		auto* cube = (const XrCompositionLayerCubeKHR*)layer;
		entry["flags"] = (Json::UInt64)cube->layerFlags;
		entry["eye_visibility"] = EyeName(cube->eyeVisibility);
		RecordPlacement(entry, cube->space, &cube->orientation, invalid);
		entry["pose"] = PoseJson({ cube->orientation, { 0, 0, 0 } });
		entry["array_index"] = cube->imageArrayIndex;
		auto it = St().swapchains.find((RtSwapchain*)cube->swapchain);
		if (it == St().swapchains.end()) {
			entry["format"] = Json::Value();
			Invalid(invalid, "unknown swapchain");
		} else {
			RtSwapchain* sc = it->second.get();
			entry["format"] = (Json::Int64)sc->info.format;
			if (sc->info.faceCount != 6)
				Invalid(invalid, "not a cube swapchain");
			if (cube->imageArrayIndex >= (sc->info.arraySize ? sc->info.arraySize : 1))
				Invalid(invalid, "array index out of range");
			if (sc->lastReleased < 0)
				Invalid(invalid, "no image released");
		}
		NeedExtension("XR_KHR_composition_layer_cube", "cube", invalid);
		break;
	}
	default:
		Invalid(invalid, "unknown layer type");
		break;
	}
	entry["invalid"] = InvalidJson(invalid);
	return entry;
}

XrResult XRAPI_CALL EndFrame(XrSession, const XrFrameEndInfo* info)
{
	FAIL_POINT("xrEndFrame");
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->frameOpen) {
		CountFrameOrderError(); // an end without a begin
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	s->frameOpen = false;
	s->framesEnded++;
	Dump& d = GetDump();
	if (!Dump::Enabled())
		return XR_SUCCESS; // nothing recorded, nothing read back
	// Whole records and pixels for the first 64 frames and every 32nd after: enough to check, and the dump stays
	// small however many frames a loop ends. The readback happens under the runtime's lock: another thread's call
	// waits for it, which makes the runtime more serialised than a real one.
	bool keep = s->framesEnded <= 64 || s->framesEnded % 32 == 0;
	std::vector<Json::Value> entries;
	std::vector<std::string> frameInvalid;
	if (info->layerCount > 16)
		frameInvalid.push_back("too many layers"); // more than maxLayerCount
	if (info->layerCount && !info->layers)
		frameInvalid.push_back("null layers");
	if (info->displayTime <= 0)
		frameInvalid.push_back("display time not positive");
	if (info->environmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
		frameInvalid.push_back("blend mode unsupported");
	for (uint32_t i = 0; i < info->layerCount && info->layers; i++)
		entries.push_back(RecordLayer(s, info->layers[i], keep));
	std::lock_guard<std::mutex> dg(d.lock);
	d.root["frames_ended"] = (Json::UInt64)s->framesEnded;
	for (const std::string& what : frameInvalid) {
		Json::Value& n = d.root["frame_invalid"][what];
		n = n.asInt() + 1;
	}
	Json::Value& layers = d.root["layers"];
	// The previous frame's records, kept only in case it was the last: dropped now
	d.root["layers_dropped"] = d.root["layers_dropped"].asUInt64() + d.lastFrame.size();
	d.lastFrame = Json::Value(Json::arrayValue);
	keep = keep && layers.size() + entries.size() <= kDumpLayerCap;
	for (Json::Value& entry : entries) {
		entry["frame"] = (Json::UInt64)s->framesEnded;
		(keep ? layers : d.lastFrame).append(entry);
	}
	return XR_SUCCESS;
}

// --- dispatch ---

XrResult XRAPI_CALL GetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);

struct Entry {
	const char* name;
	PFN_xrVoidFunction fn;
	const char* extension; // nullptr for core
};

#define E(n, f) { n, (PFN_xrVoidFunction)f, nullptr }
#define X(n, f, ext) { n, (PFN_xrVoidFunction)f, ext }
const Entry kEntries[] = {
	E("xrGetInstanceProcAddr", GetInstanceProcAddr),
	E("xrEnumerateApiLayerProperties", EnumerateApiLayerProperties),
	E("xrEnumerateInstanceExtensionProperties", EnumerateInstanceExtensionProperties),
	E("xrCreateInstance", CreateInstance),
	E("xrDestroyInstance", DestroyInstance),
	E("xrGetInstanceProperties", GetInstanceProperties),
	E("xrPollEvent", PollEvent),
	E("xrResultToString", ResultToString),
	E("xrStructureTypeToString", StructureTypeToString),
	E("xrStringToPath", StringToPath),
	E("xrPathToString", PathToString),
	E("xrGetSystem", GetSystem),
	E("xrGetSystemProperties", GetSystemProperties),
	E("xrEnumerateEnvironmentBlendModes", EnumerateEnvironmentBlendModes),
	E("xrEnumerateViewConfigurations", EnumerateViewConfigurations),
	E("xrGetViewConfigurationProperties", GetViewConfigurationProperties),
	E("xrEnumerateViewConfigurationViews", EnumerateViewConfigurationViews),
	E("xrCreateSession", CreateSession),
	E("xrDestroySession", DestroySession),
	E("xrBeginSession", BeginSession),
	E("xrEndSession", EndSession),
	E("xrRequestExitSession", RequestExitSession),
	E("xrEnumerateReferenceSpaces", EnumerateReferenceSpaces),
	E("xrCreateReferenceSpace", CreateReferenceSpace),
	E("xrDestroySpace", DestroySpace),
	E("xrLocateSpace", LocateSpace),
	E("xrGetReferenceSpaceBoundsRect", GetReferenceSpaceBoundsRect),
	E("xrLocateViews", LocateViews),
	E("xrEnumerateSwapchainFormats", EnumerateSwapchainFormats),
	E("xrCreateSwapchain", CreateSwapchain),
	E("xrDestroySwapchain", DestroySwapchain),
	E("xrEnumerateSwapchainImages", EnumerateSwapchainImages),
	E("xrAcquireSwapchainImage", AcquireSwapchainImage),
	E("xrWaitSwapchainImage", WaitSwapchainImage),
	E("xrReleaseSwapchainImage", ReleaseSwapchainImage),
	E("xrWaitFrame", WaitFrame),
	E("xrBeginFrame", BeginFrame),
	E("xrEndFrame", EndFrame),
	X("xrGetD3D11GraphicsRequirementsKHR", GetD3D11GraphicsRequirements, "XR_KHR_D3D11_enable"),
};
#undef E
#undef X

XrResult XRAPI_CALL GetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function)
{
	*function = nullptr;
	for (const Entry& e : kEntries) {
		if (std::strcmp(e.name, name) != 0)
			continue;
		if (instance == XR_NULL_HANDLE) {
			// Only these may be asked for without an instance
			bool preInstance = !std::strcmp(name, "xrEnumerateInstanceExtensionProperties")
			    || !std::strcmp(name, "xrEnumerateApiLayerProperties") || !std::strcmp(name, "xrCreateInstance")
			    || !std::strcmp(name, "xrGetInstanceProcAddr");
			if (!preInstance)
				return XR_ERROR_HANDLE_INVALID;
		}
		if (e.extension && !St().enabledExtensions.count(e.extension))
			return XR_ERROR_FUNCTION_UNSUPPORTED;
		*function = e.fn;
		return XR_SUCCESS;
	}
	if (PFN_xrVoidFunction fn = RtActionsLookup(name)) {
		if (instance == XR_NULL_HANDLE)
			return XR_ERROR_HANDLE_INVALID;
		*function = fn;
		return XR_SUCCESS;
	}
	const char* extension = nullptr;
	if (PFN_xrVoidFunction fn = RtExtensionsLookup(name, &extension)) {
		if (instance == XR_NULL_HANDLE)
			return XR_ERROR_HANDLE_INVALID;
		if (extension && !RtExtensionEnabled(extension))
			return XR_ERROR_FUNCTION_UNSUPPORTED;
		*function = fn;
		return XR_SUCCESS;
	}
	return XR_ERROR_FUNCTION_UNSUPPORTED;
}

} // namespace

extern "C" XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* request)
{
	if (!loaderInfo || !request || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO
	    || request->structType != XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST)
		return XR_ERROR_INITIALIZATION_FAILED;
	request->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
	request->runtimeApiVersion = XR_CURRENT_API_VERSION;
	request->getInstanceProcAddr = GetInstanceProcAddr;
	return XR_SUCCESS;
}
