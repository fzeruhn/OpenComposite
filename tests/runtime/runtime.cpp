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

#include <openxr/openxr_reflection.h>

#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

namespace {

// --- poses ---

XrQuaternionf QMul(const XrQuaternionf& a, const XrQuaternionf& b)
{
	return {
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
	};
}

XrQuaternionf QConj(const XrQuaternionf& q) { return { -q.x, -q.y, -q.z, q.w }; }

XrVector3f QRotate(const XrQuaternionf& q, const XrVector3f& v)
{
	XrQuaternionf p{ v.x, v.y, v.z, 0 };
	XrQuaternionf r = QMul(QMul(q, p), QConj(q));
	return { r.x, r.y, r.z };
}

XrPosef Compose(const XrPosef& a, const XrPosef& b) // a * b: b expressed in a's frame
{
	XrVector3f p = QRotate(a.orientation, b.position);
	return { QMul(a.orientation, b.orientation), { a.position.x + p.x, a.position.y + p.y, a.position.z + p.z } };
}

XrPosef Inverse(const XrPosef& a)
{
	XrQuaternionf inv = QConj(a.orientation);
	XrVector3f p = QRotate(inv, a.position);
	return { inv, { -p.x, -p.y, -p.z } };
}

const XrPosef kIdentity{ { 0, 0, 0, 1 }, { 0, 0, 0 } };
const XrPosef kHead{ { 0, 0, 0, 1 }, { 0, 1.7f, 0 } };

// --- objects ---

struct RtSpace {
	XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_LOCAL;
	XrPosef offset = kIdentity;
	bool isAction = false;
	XrPosef actionPose = kIdentity; // pose in STAGE for action spaces
};

struct RtState {
	std::mutex lock;
	bool instanceAlive = false;
	std::set<std::string> enabledExtensions;
	std::unique_ptr<RtSession> session;
	std::map<RtSpace*, std::unique_ptr<RtSpace>> spaces;
	std::map<RtSwapchain*, std::unique_ptr<RtSwapchain>> swapchains;
	std::deque<XrEventDataBuffer> events;
	std::map<std::string, XrPath> pathIds;
	std::vector<std::string> paths{ "" };
	XrTime time = 1000000000;
};

RtState& St()
{
	static RtState state;
	return state;
}

XrInstance kInstance = (XrInstance)(uintptr_t)0x1001;
const XrSystemId kSystem = 1;

XrTime Period() { return (XrTime)(1e9 / GetScript().displayHz); }

void QueueState(RtSession* s, XrSessionState state)
{
	s->state = state;
	XrEventDataBuffer buf{ XR_TYPE_EVENT_DATA_BUFFER };
	auto* ev = (XrEventDataSessionStateChanged*)&buf;
	ev->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
	ev->session = (XrSession)s;
	ev->state = state;
	ev->time = St().time;
	St().events.push_back(buf);
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
	XrPosef base = sp->isAction ? sp->actionPose : (sp->type == XR_REFERENCE_SPACE_TYPE_VIEW ? kHead : kIdentity);
	return Compose(base, sp->offset);
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
	std::lock_guard<std::mutex> g(St().lock);
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
	}
	*instance = kInstance;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroyInstance(XrInstance)
{
	{
		std::lock_guard<std::mutex> g(St().lock);
		for (auto& [p, sc] : St().swapchains)
			RtD3D11DestroySwapchain(sc.get());
		St().swapchains.clear();
		St().spaces.clear();
		if (St().session && St().session->device)
			St().session->device->Release();
		St().session.reset();
		St().events.clear();
		St().instanceAlive = false;
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
	std::lock_guard<std::mutex> g(St().lock);
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
	std::lock_guard<std::mutex> g(St().lock);
	auto it = St().pathIds.find(str);
	if (it != St().pathIds.end()) {
		*path = it->second;
		return XR_SUCCESS;
	}
	XrPath id = St().paths.size();
	St().paths.push_back(str);
	St().pathIds[str] = id;
	*path = id;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL PathToString(XrInstance, XrPath path, uint32_t capacity, uint32_t* count, char* buffer)
{
	std::lock_guard<std::mutex> g(St().lock);
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
	return RtD3D11Requirements(reqs);
}

// --- session ---

XrResult XRAPI_CALL CreateSession(XrInstance, const XrSessionCreateInfo* info, XrSession* out)
{
	FAIL_POINT("xrCreateSession");
	std::lock_guard<std::mutex> g(St().lock);
	if (St().session)
		return XR_ERROR_LIMIT_REACHED;
	auto session = std::make_unique<RtSession>();
	std::string binding = "none";
	for (auto* next = (const XrBaseInStructure*)info->next; next; next = next->next) {
		if (next->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
			session->device = ((const XrGraphicsBindingD3D11KHR*)next)->device;
			binding = "d3d11";
		}
	}
	if (!session->device && !session->headless)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	if (session->device)
		session->device->AddRef();
	{
		Dump& d = GetDump();
		std::lock_guard<std::mutex> dg(d.lock);
		d.root["session"]["binding"] = binding;
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
	std::lock_guard<std::mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s || (XrSession)s != session)
		return XR_ERROR_HANDLE_INVALID;
	for (auto& [p, sc] : St().swapchains)
		RtD3D11DestroySwapchain(sc.get());
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
	std::lock_guard<std::mutex> g(St().lock);
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
	std::lock_guard<std::mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	s->running = false;
	QueueState(s, XR_SESSION_STATE_IDLE);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL RequestExitSession(XrSession)
{
	std::lock_guard<std::mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s || !s->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
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
	std::lock_guard<std::mutex> g(St().lock);
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
	std::lock_guard<std::mutex> g(St().lock);
	St().spaces.erase((RtSpace*)space);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL LocateSpace(XrSpace space, XrSpace base, XrTime, XrSpaceLocation* loc)
{
	FAIL_POINT("xrLocateSpace");
	std::lock_guard<std::mutex> g(St().lock);
	auto a = St().spaces.find((RtSpace*)space), b = St().spaces.find((RtSpace*)base);
	if (a == St().spaces.end() || b == St().spaces.end())
		return XR_ERROR_HANDLE_INVALID;
	loc->pose = Compose(Inverse(SpaceInStage(b->second.get())), SpaceInStage(a->second.get()));
	loc->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT
	    | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
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
	std::lock_guard<std::mutex> g(St().lock);
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
	for (int i = 0; i < 2; i++) {
		XrPosef eye{ { 0, 0, 0, 1 }, { (i == 0 ? -0.5f : 0.5f) * GetScript().ipd, 0, 0 } };
		views[i].pose = Compose(headInBase, eye);
		views[i].fov = { -half, half, half, -half };
	}
	state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT
	    | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
	return XR_SUCCESS;
}

// --- swapchains ---

XrResult XRAPI_CALL EnumerateSwapchainFormats(XrSession, uint32_t capacity, uint32_t* count, int64_t* formats)
{
	return TwoCall(capacity, count, formats, RtD3D11Formats());
}

XrResult XRAPI_CALL CreateSwapchain(XrSession, const XrSwapchainCreateInfo* info, XrSwapchain* out)
{
	FAIL_POINT("xrCreateSwapchain");
	std::lock_guard<std::mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
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
	std::lock_guard<std::mutex> g(St().lock);
	auto it = St().swapchains.find((RtSwapchain*)swapchain);
	if (it == St().swapchains.end())
		return XR_ERROR_HANDLE_INVALID;
	RtD3D11DestroySwapchain(it->second.get());
	St().swapchains.erase(it);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL EnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images)
{
	std::lock_guard<std::mutex> g(St().lock);
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
	std::lock_guard<std::mutex> g(St().lock);
	auto it = St().swapchains.find((RtSwapchain*)swapchain);
	if (it == St().swapchains.end())
		return XR_ERROR_HANDLE_INVALID;
	RtSwapchain* sc = it->second.get();
	if (sc->acquired >= (int)sc->textures.size())
		return XR_ERROR_CALL_ORDER_INVALID;
	*index = sc->next;
	sc->next = (sc->next + 1) % (uint32_t)sc->textures.size();
	sc->acquired++;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL WaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*)
{
	FAIL_POINT("xrWaitSwapchainImage");
	return XR_SUCCESS;
}

XrResult XRAPI_CALL ReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*)
{
	FAIL_POINT("xrReleaseSwapchainImage");
	std::lock_guard<std::mutex> g(St().lock);
	auto it = St().swapchains.find((RtSwapchain*)swapchain);
	if (it == St().swapchains.end())
		return XR_ERROR_HANDLE_INVALID;
	if (it->second->acquired <= 0)
		return XR_ERROR_CALL_ORDER_INVALID;
	it->second->acquired--;
	return XR_SUCCESS;
}

// --- frames ---

XrResult XRAPI_CALL WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* fs)
{
	FAIL_POINT("xrWaitFrame");
	std::lock_guard<std::mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
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
	St().time += Period();
	fs->predictedDisplayTime = St().time + Period();
	fs->predictedDisplayPeriod = Period();
	fs->shouldRender = (s->state == XR_SESSION_STATE_VISIBLE || s->state == XR_SESSION_STATE_FOCUSED) ? XR_TRUE : XR_FALSE;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL BeginFrame(XrSession, const XrFrameBeginInfo*)
{
	FAIL_POINT("xrBeginFrame");
	std::lock_guard<std::mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	if (s->framesBegun >= s->framesWaited)
		return XR_ERROR_CALL_ORDER_INVALID;
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

XrResult XRAPI_CALL EndFrame(XrSession, const XrFrameEndInfo* info)
{
	FAIL_POINT("xrEndFrame");
	std::lock_guard<std::mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->frameOpen)
		return XR_ERROR_CALL_ORDER_INVALID;
	s->frameOpen = false;
	s->framesEnded++;
	Dump& d = GetDump();
	std::lock_guard<std::mutex> dg(d.lock);
	d.root["frames_ended"] = (Json::UInt64)s->framesEnded;
	for (uint32_t i = 0; i < info->layerCount; i++) {
		const XrCompositionLayerBaseHeader* layer = info->layers[i];
		Json::Value entry;
		entry["frame"] = (Json::UInt64)s->framesEnded;
		entry["type"] = LayerTypeName(layer->type);
		bool depth = false;
		if (layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
			auto* proj = (const XrCompositionLayerProjection*)layer;
			for (uint32_t v = 0; v < proj->viewCount; v++)
				for (auto* n = (const XrBaseInStructure*)proj->views[v].next; n; n = n->next)
					depth = depth || n->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;
		}
		entry["depth"] = depth;
		d.root["layers"].append(entry);
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
	return XR_ERROR_FUNCTION_UNSUPPORTED;
}

} // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* request)
{
	if (!loaderInfo || !request || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO
	    || request->structType != XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST)
		return XR_ERROR_INITIALIZATION_FAILED;
	request->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
	request->runtimeApiVersion = XR_CURRENT_API_VERSION;
	request->getInstanceProcAddr = GetInstanceProcAddr;
	return XR_SUCCESS;
}
