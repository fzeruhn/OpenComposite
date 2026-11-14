// The stand-in runtime's optional extensions. Each is offered only when the script lists it, and its functions only
// once it is enabled. Structures the bundled 1.0.12 headers lack are declared here with their registry values.

#include "runtime_internal.h"

#include "XR_MNDX_xdev_space.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// --- XR_EXT_hand_tracking (+ XR_EXT_hand_tracking_data_source) ---

// XR_EXT_hand_tracking_data_source isn't in the 1.0.12 headers
const XrStructureType kTypeHandTrackingDataSourceState = (XrStructureType)1000428001;
struct HandTrackingDataSourceState {
	XrStructureType type;
	void* next;
	XrBool32 isActive;
	int32_t dataSource; // XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT = 2
};

struct RtHandTracker {
	XrHandEXT hand = XR_HAND_RIGHT_EXT;
};

XrResult XRAPI_CALL CreateHandTracker(XrSession, const XrHandTrackerCreateInfoEXT* info, XrHandTrackerEXT* out)
{
	auto* t = new RtHandTracker;
	t->hand = info->hand;
	*out = (XrHandTrackerEXT)t;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroyHandTracker(XrHandTrackerEXT tracker)
{
	delete (RtHandTracker*)tracker;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL LocateHandJoints(XrHandTrackerEXT tracker, const XrHandJointsLocateInfoEXT* info, XrHandJointLocationsEXT* locations)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto base = St().spaces.find((RtSpace*)info->baseSpace);
	if (base == St().spaces.end())
		return XR_ERROR_HANDLE_INVALID;
	int hand = ((RtHandTracker*)tracker)->hand == XR_HAND_LEFT_EXT ? 0 : 1;
	XrPosef palm = HandPose(hand);
	XrPosef baseInStage = base->second->isAction ? base->second->actionPose
	                                             : (base->second->type == XR_REFERENCE_SPACE_TYPE_VIEW ? kHead : kIdentity);
	baseInStage = Compose(baseInStage, base->second->offset);
	XrPosef toBase = Inverse(baseInStage);
	// A fixed rest pose: joints laid out along -Z from the palm, 1 cm apart
	for (uint32_t j = 0; j < locations->jointCount; j++) {
		XrPosef joint = palm;
		joint.position.z -= 0.01f * (float)j;
		locations->jointLocations[j].pose = Compose(toBase, joint);
		locations->jointLocations[j].radius = 0.01f;
		locations->jointLocations[j].locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
		    | XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
		    | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	}
	locations->isActive = XR_TRUE;
	for (auto* n = (XrBaseOutStructure*)locations->next; n; n = n->next) {
		if (n->type == kTypeHandTrackingDataSourceState) {
			auto* state = (HandTrackingDataSourceState*)n;
			state->isActive = XR_TRUE;
			state->dataSource = 2;
		}
	}
	return XR_SUCCESS;
}

// --- XR_MNDX_xdev_space: one tracker ---

const XrXDevIdMNDX kTrackerId = 1;

XrResult XRAPI_CALL CreateXDevList(XrSession, const XrCreateXDevListInfoMNDX*, XrXDevListMNDX* list)
{
	*list = (XrXDevListMNDX)(uintptr_t)0x2001;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetXDevListGenerationNumber(XrXDevListMNDX, uint64_t* generation)
{
	*generation = 1;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL EnumerateXDevs(XrXDevListMNDX, uint32_t capacity, uint32_t* count, XrXDevIdMNDX* xdevs)
{
	*count = 1;
	if (capacity == 0)
		return XR_SUCCESS;
	xdevs[0] = kTrackerId;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetXDevProperties(XrXDevListMNDX, const XrGetXDevInfoMNDX* info, XrXDevPropertiesMNDX* props)
{
	if (info->id != kTrackerId)
		return XR_ERROR_HANDLE_INVALID;
	std::snprintf(props->name, sizeof(props->name), "Test Tracker");
	std::snprintf(props->serial, sizeof(props->serial), "TEST-0001");
	props->canCreateSpace = XR_TRUE;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroyXDevList(XrXDevListMNDX)
{
	return XR_SUCCESS;
}

XrResult XRAPI_CALL CreateXDevSpace(XrSession, const XrCreateXDevSpaceInfoMNDX* info, XrSpace* out)
{
	if (info->id != kTrackerId)
		return XR_ERROR_HANDLE_INVALID;
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto sp = std::make_unique<RtSpace>();
	sp->isAction = true;
	sp->actionPose = { { 0, 0, 0, 1 }, { 0, 1.0f, 0 } }; // at the waist
	sp->offset = info->offset;
	RtSpace* raw = sp.get();
	St().spaces[raw] = std::move(sp);
	*out = (XrSpace)raw;
	return XR_SUCCESS;
}

// --- XR_KHR_visibility_mask: an octagon ---

XrResult XRAPI_CALL GetVisibilityMask(XrSession, XrViewConfigurationType, uint32_t, XrVisibilityMaskTypeKHR type, XrVisibilityMaskKHR* mask)
{
	std::vector<XrVector2f> verts;
	std::vector<uint32_t> indices;
	const int n = 8;
	if (type == XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR) {
		for (int i = 0; i < n; i++) {
			float a = 6.2831853f * i / n;
			verts.push_back({ std::cos(a), std::sin(a) });
			indices.push_back(i);
		}
	} else {
		// Triangles of the octagon's area (visible) or of what's outside it within the unit square (hidden)
		verts.push_back({ 0, 0 });
		for (int i = 0; i < n; i++) {
			float a = 6.2831853f * i / n;
			verts.push_back({ std::cos(a), std::sin(a) });
		}
		for (int i = 0; i < n; i++) {
			indices.push_back(0);
			indices.push_back(1 + i);
			indices.push_back(1 + (i + 1) % n);
		}
	}
	mask->vertexCountOutput = (uint32_t)verts.size();
	mask->indexCountOutput = (uint32_t)indices.size();
	if (mask->vertexCapacityInput == 0 || mask->indexCapacityInput == 0)
		return XR_SUCCESS;
	if (mask->vertexCapacityInput < verts.size() || mask->indexCapacityInput < indices.size())
		return XR_ERROR_SIZE_INSUFFICIENT;
	std::memcpy(mask->vertices, verts.data(), verts.size() * sizeof(XrVector2f));
	std::memcpy(mask->indices, indices.data(), indices.size() * sizeof(uint32_t));
	return XR_SUCCESS;
}

// --- XR_FB_display_refresh_rate (not in the 1.0.12 headers): the script's refresh_rates ---

const XrResult kErrorDisplayRefreshRateUnsupported = (XrResult)-1000101000; // XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB
const XrStructureType kTypeEventDisplayRefreshRateChanged = (XrStructureType)1000101000;
struct EventDisplayRefreshRateChanged {
	XrStructureType type;
	const void* next;
	XrSession session;
	float fromDisplayRefreshRate;
	float toDisplayRefreshRate;
};

std::vector<float> RefreshRates()
{
	const std::vector<float>& scripted = GetScript().refreshRates;
	return scripted.empty() ? std::vector<float>{ GetScript().displayHz } : scripted;
}

XrResult XRAPI_CALL EnumerateDisplayRefreshRates(XrSession, uint32_t capacity, uint32_t* count, float* rates)
{
	std::vector<float> list = RefreshRates();
	*count = (uint32_t)list.size();
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < list.size())
		return XR_ERROR_SIZE_INSUFFICIENT;
	for (size_t i = 0; i < list.size(); i++)
		rates[i] = list[i];
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetDisplayRefreshRate(XrSession, float* rate)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	*rate = St().session ? St().session->refreshRate : RefreshRates()[0];
	return XR_SUCCESS;
}

// Counted in the dump (refresh_rate_requests, and the last rate asked for); 0 asks for the runtime's choice, the first
// rate. A change is announced with XrEventDataDisplayRefreshRateChangedFB.
XrResult XRAPI_CALL RequestDisplayRefreshRate(XrSession, float rate)
{
	{
		Dump& d = GetDump();
		std::lock_guard<std::mutex> dg(d.lock);
		d.root["refresh_rate_requests"] = d.root["refresh_rate_requests"].asInt() + 1;
		d.root["refresh_rate_last_requested"] = rate;
	}
	std::vector<float> list = RefreshRates();
	float to = rate == 0 ? list[0] : rate;
	if (std::find(list.begin(), list.end(), to) == list.end())
		return kErrorDisplayRefreshRateUnsupported;
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = St().session.get();
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (s->refreshRate != to) {
		XrEventDataBuffer buf{ XR_TYPE_EVENT_DATA_BUFFER };
		auto* ev = (EventDisplayRefreshRateChanged*)&buf;
		ev->type = kTypeEventDisplayRefreshRateChanged;
		ev->next = nullptr;
		ev->session = (XrSession)s;
		ev->fromDisplayRefreshRate = s->refreshRate;
		ev->toDisplayRefreshRate = to;
		s->refreshRate = to;
		QueueEvent(buf);
	}
	return XR_SUCCESS;
}

} // namespace

PFN_xrVoidFunction RtExtensionsLookup(const char* name, const char** extension)
{
	struct Entry {
		const char* name;
		PFN_xrVoidFunction fn;
		const char* extension;
	};
	static const Entry entries[] = {
		{ "xrCreateHandTrackerEXT", (PFN_xrVoidFunction)CreateHandTracker, "XR_EXT_hand_tracking" },
		{ "xrDestroyHandTrackerEXT", (PFN_xrVoidFunction)DestroyHandTracker, "XR_EXT_hand_tracking" },
		{ "xrLocateHandJointsEXT", (PFN_xrVoidFunction)LocateHandJoints, "XR_EXT_hand_tracking" },
		{ "xrCreateXDevListMNDX", (PFN_xrVoidFunction)CreateXDevList, "XR_MNDX_xdev_space" },
		{ "xrGetXDevListGenerationNumberMNDX", (PFN_xrVoidFunction)GetXDevListGenerationNumber, "XR_MNDX_xdev_space" },
		{ "xrEnumerateXDevsMNDX", (PFN_xrVoidFunction)EnumerateXDevs, "XR_MNDX_xdev_space" },
		{ "xrGetXDevPropertiesMNDX", (PFN_xrVoidFunction)GetXDevProperties, "XR_MNDX_xdev_space" },
		{ "xrDestroyXDevListMNDX", (PFN_xrVoidFunction)DestroyXDevList, "XR_MNDX_xdev_space" },
		{ "xrCreateXDevSpaceMNDX", (PFN_xrVoidFunction)CreateXDevSpace, "XR_MNDX_xdev_space" },
		{ "xrGetVisibilityMaskKHR", (PFN_xrVoidFunction)GetVisibilityMask, "XR_KHR_visibility_mask" },
		{ "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction)EnumerateDisplayRefreshRates, "XR_FB_display_refresh_rate" },
		{ "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction)GetDisplayRefreshRate, "XR_FB_display_refresh_rate" },
		{ "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction)RequestDisplayRefreshRate, "XR_FB_display_refresh_rate" },
	};
	for (const Entry& e : entries) {
		if (!std::strcmp(e.name, name)) {
			*extension = e.extension;
			return e.fn;
		}
	}
	return nullptr;
}

void RtExtensionsSystemProperties(XrSystemProperties* props)
{
	for (auto* n = (XrBaseOutStructure*)props->next; n; n = n->next) {
		if (n->type == XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT)
			((XrSystemHandTrackingPropertiesEXT*)n)->supportsHandTracking = RtExtensionEnabled("XR_EXT_hand_tracking");
		else if (n->type == XR_TYPE_SYSTEM_XDEV_SPACE_PROPERTIES_MNDX)
			((XrSystemXDevSpacePropertiesMNDX*)n)->supportsXDevSpace = RtExtensionEnabled("XR_MNDX_xdev_space");
		else if (n->type == XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT)
			((XrSystemEyeGazeInteractionPropertiesEXT*)n)->supportsEyeGazeInteraction = RtExtensionEnabled("XR_EXT_eye_gaze_interaction");
		else if (n->type == (XrStructureType)1000470001) // XR_TYPE_SYSTEM_USER_PRESENCE_PROPERTIES_EXT
			((XrSystemEyeGazeInteractionPropertiesEXT*)n)->supportsEyeGazeInteraction = RtExtensionEnabled("XR_EXT_user_presence"); // same layout: type, next, XrBool32
	}
}

bool RtExtensionsSessionInfo(const XrSessionCreateInfo* info, RtSession* session)
{
	bool hasBinding = false;
	for (auto* n = (const XrBaseInStructure*)info->next; n; n = n->next) {
		if (n->type == XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX) {
			if (!RtExtensionEnabled("XR_EXTX_overlay"))
				return false;
			session->overlay = true;
		}
		if (n->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)
			hasBinding = true;
	}
	if (!hasBinding && RtExtensionEnabled("XR_MND_headless"))
		session->headless = true;
	return true;
}
