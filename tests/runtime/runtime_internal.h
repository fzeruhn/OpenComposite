#pragma once
// Internals of the stand-in OpenXR runtime (oc_test_runtime).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>

// The project defines every graphics API for every target; this runtime only does D3D11
#undef XR_USE_GRAPHICS_API_VULKAN
#undef XR_USE_GRAPHICS_API_OPENGL
#undef XR_USE_GRAPHICS_API_OPENGL_ES
#undef XR_USE_GRAPHICS_API_D3D12
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <loader_interfaces.h>

#include "script.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// --- poses ---

inline XrQuaternionf QMul(const XrQuaternionf& a, const XrQuaternionf& b)
{
	return {
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
	};
}

inline XrQuaternionf QConj(const XrQuaternionf& q) { return { -q.x, -q.y, -q.z, q.w }; }

inline XrVector3f QRotate(const XrQuaternionf& q, const XrVector3f& v)
{
	XrQuaternionf p{ v.x, v.y, v.z, 0 };
	XrQuaternionf r = QMul(QMul(q, p), QConj(q));
	return { r.x, r.y, r.z };
}

inline XrPosef Compose(const XrPosef& a, const XrPosef& b) // a * b: b expressed in a's frame
{
	XrVector3f p = QRotate(a.orientation, b.position);
	return { QMul(a.orientation, b.orientation), { a.position.x + p.x, a.position.y + p.y, a.position.z + p.z } };
}

inline XrPosef Inverse(const XrPosef& a)
{
	XrQuaternionf inv = QConj(a.orientation);
	XrVector3f p = QRotate(inv, a.position);
	return { inv, { -p.x, -p.y, -p.z } };
}

inline const XrPosef kIdentity{ { 0, 0, 0, 1 }, { 0, 0, 0 } };
inline const XrPosef kHead{ { 0, 0, 0, 1 }, { 0, 1.7f, 0 } };

// Grip and aim pose of a hand in STAGE (both the same), 0 = left, 1 = right
inline XrPosef HandPose(int hand) { return { { 0, 0, 0, 1 }, { hand == 0 ? -0.2f : 0.2f, 1.4f, -0.3f } }; }

// --- objects ---

struct RtActionSet {
	std::string name;
	bool attached = false;
};

struct RtAction {
	RtActionSet* set = nullptr;
	std::string name;
	XrActionType type = XR_ACTION_TYPE_BOOLEAN_INPUT;
	std::vector<XrPath> subactionPaths;
};

struct RtSpace {
	XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_LOCAL;
	XrPosef offset = kIdentity;
	bool isAction = false;
	XrPosef actionPose = kIdentity; // pose in STAGE for action spaces
};

struct RtSwapchain {
	XrSwapchainCreateInfo info{};
	std::vector<ID3D11Texture2D*> textures;
	uint32_t next = 0;
	int acquired = 0;
};

struct RtSession {
	ID3D11Device* device = nullptr;
	bool headless = false;
	bool overlay = false;
	bool running = false;
	bool exitRequested = false; // xrRequestExitSession: xrEndSession then goes on to EXITING, as the spec says
	XrSessionState state = XR_SESSION_STATE_UNKNOWN;
	uint64_t framesWaited = 0;
	uint64_t framesBegun = 0;
	uint64_t framesEnded = 0;
	bool frameOpen = false;
	bool actionsAttached = false;
	XrPath currentProfile = XR_NULL_PATH; // set at the first xrSyncActions
	std::set<RtActionSet*> activeSets;
	std::map<std::string, float> inputValues; // as of the last xrSyncActions
	float refreshRate = 90;
};

struct RtState {
	std::recursive_mutex lock;
	bool instanceAlive = false;
	std::set<std::string> enabledExtensions;
	std::unique_ptr<RtSession> session;
	std::map<RtSpace*, std::unique_ptr<RtSpace>> spaces;
	std::map<RtSwapchain*, std::unique_ptr<RtSwapchain>> swapchains;
	std::map<RtActionSet*, std::unique_ptr<RtActionSet>> actionSets;
	std::map<RtAction*, std::unique_ptr<RtAction>> actions;
	// profile path → (action, binding path) pairs suggested for it
	std::map<XrPath, std::vector<std::pair<RtAction*, XrPath>>> suggested;
	std::deque<XrEventDataBuffer> events;
	std::map<std::string, XrPath> pathIds;
	std::vector<std::string> paths{ "" };
	XrTime time = 1000000000;
	bool presenceSent = false;
};

RtState& St();
XrPath RtPath(const std::string& str); // interns a path (lock held or not)
std::string RtPathString(XrPath path);
void QueueState(RtSession* s, XrSessionState state);
void QueueEvent(const XrEventDataBuffer& ev);
bool RtExtensionEnabled(const char* name);

// swapchain_d3d11.cpp
XrResult RtD3D11Requirements(XrGraphicsRequirementsD3D11KHR* reqs);
XrResult RtD3D11CreateSwapchain(RtSession* session, const XrSwapchainCreateInfo* info, RtSwapchain* sc);
void RtD3D11DestroySwapchain(RtSwapchain* sc);
const std::vector<int64_t>& RtD3D11Formats();

// actions.cpp
PFN_xrVoidFunction RtActionsLookup(const char* name);
void RtActionsOnFrame(RtSession* s); // per xrWaitFrame (presence events)
void RtActionsReset();
void RtActionsDump(Json::Value& root);

// extensions.cpp: functions of optional extensions; *extension gets the extension that must be enabled
PFN_xrVoidFunction RtExtensionsLookup(const char* name, const char** extension);
void RtExtensionsSystemProperties(XrSystemProperties* props);
bool RtExtensionsSessionInfo(const XrSessionCreateInfo* info, RtSession* session); // headless / overlay chains
