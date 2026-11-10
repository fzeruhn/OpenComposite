// The named cases of oc_conformance: sequences the per-method callers can't do (real handles, frames, several Init
// rounds). Each writes one record per step: {"case", "method": <step label>, ...}.

#include "conformance.h"

#include "IVRApplications_001.h"
#include "IVRApplications_007.h"
#include "IVRChaperone_002.h"
#include "IVRChaperone_004.h"
#include "IVRCompositor_005.h"
#include "IVRControlPanel_001.h"
#include "IVRDriverManager_001.h"
#include "custom_interfaces/IVRPaths_002.h"
#include "IVRSpatialAnchors_001.h"
#include "IVRTrackedCamera_006.h"
#include "IVRInput_003.h"
#include "IVRNotifications_001.h"
#include "IVRRenderModels_001.h"
#include "IVRCompositor_006.h"
#include "IVRCompositor_008.h"
#include "IVRCompositor_029.h"
#include "IVRInput_010.h"
#include "IVROverlay_001.h"
#include "IVROverlay_006.h"
#include "IVROverlay_007.h"
#include "IVROverlay_028.h"
#include "IVRSystem_003.h"
#include "IVRSystem_007.h"
#include "IVRSystem_009.h"
#include "IVRSystem_022.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace conformance;

namespace {

Json::Value Err(int e)
{
	Json::Value v;
	v["ret"] = e;
	return v;
}

void InitMatrix(CaseContext& c)
{
	std::vector<int> types;
	for (int t = 0; t <= 13; t++)
		types.push_back(t);
	types.push_back(99);
	for (int t : types) {
		vr::EVRInitError e = c.core->Init((vr::EVRApplicationType)t, nullptr);
		Json::Value r;
		r["init_error"] = (int)e;
		c.Record("Init(" + std::to_string(t) + ")", r);
		if (e == vr::VRInitError_None)
			c.core->Cleanup();
	}
	Json::Value r;
	r["ret"] = c.core->BIsHmdPresent();
	c.Record("BIsHmdPresent", r);
}

void InitRepeat(CaseContext& c)
{
	int ok = 0, found = 0;
	vr::EVRInitError last = vr::VRInitError_None;
	for (int i = 0; i < 200; i++) {
		last = c.core->Init(vr::VRApplication_Scene, nullptr);
		if (last != vr::VRInitError_None)
			continue;
		ok++;
		vr::EVRInitError le;
		if (c.core->GetGenericInterface("IVRSystem_022", &le))
			found++;
		c.core->Cleanup();
	}
	Json::Value r;
	r["inits_ok"] = ok;
	r["interfaces_found"] = found;
	r["last_init_error"] = (int)last;
	c.Record("200 rounds", r);
}

void InitOtherThread(CaseContext& c)
{
	vr::EVRInitError e = vr::VRInitError_None;
	std::thread t([&] { e = c.core->Init(vr::VRApplication_Scene, nullptr); });
	t.join();
	Json::Value r;
	r["init_error"] = (int)e;
	c.Record("Init on another thread", r);
	if (e != vr::VRInitError_None)
		return;
	vr::EVRInitError le;
	auto* sys = (vr::IVRSystem_022::IVRSystem*)c.core->GetGenericInterface("IVRSystem_022", &le);
	Json::Value r2;
	r2["lookup_error"] = (int)le;
	if (sys) {
		uint32_t w = 0, h = 0;
		sys->GetRecommendedRenderTargetSize(&w, &h);
		r2["width"] = w;
		r2["height"] = h;
	}
	c.Record("GetRecommendedRenderTargetSize on the main thread", r2);
	c.core->Cleanup();
}

void LookupBeforeInit(CaseContext& c)
{
	auto lookups = [&](const char* when) {
		for (const std::string& v : GeneratedVersions()) {
			vr::EVRInitError le = vr::VRInitError_None;
			void* p = c.core->GetGenericInterface(v.c_str(), &le);
			Json::Value r;
			r["lookup_error"] = (int)le;
			r["found"] = p != nullptr;
			c.Record(std::string(when) + " " + v, r);
		}
	};
	lookups("before Init:");
	if (c.core->Init(vr::VRApplication_Scene, nullptr) == vr::VRInitError_None)
		c.core->Cleanup();
	lookups("after Cleanup:");
}

void UnknownInterface(CaseContext& c)
{
	vr::EVRInitError e = c.core->Init(vr::VRApplication_Scene, nullptr);
	for (const char* name : { "IVRNope_001", "IVRSystem_999", "FnTable:IVRSystem_022", "FnTable:IVRNope_001" }) {
		vr::EVRInitError le = vr::VRInitError_None;
		void* p = c.core->GetGenericInterface(name, &le);
		Json::Value r;
		r["init_error"] = (int)e;
		r["lookup_error"] = (int)le;
		r["found"] = p != nullptr;
		c.Record(name, r);
	}
	if (e == vr::VRInitError_None)
		c.core->Cleanup();
}

void OverlayLifecycle(CaseContext& c)
{
	using namespace vr::IVROverlay_028;
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le;
	auto* ov = (IVROverlay*)c.core->GetGenericInterface("IVROverlay_028", &le);
	if (!ov) {
		c.Record("no IVROverlay_028", Err(le));
		c.core->Cleanup();
		return;
	}
	vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
	c.Record("CreateOverlay", Err(ov->CreateOverlay("oc.test", "Test", &h)));

	using SetFloat = vr::EVROverlayError (IVROverlay::*)(vr::VROverlayHandle_t, float);
	using GetFloat = vr::EVROverlayError (IVROverlay::*)(vr::VROverlayHandle_t, float*);
	auto setGetFloat = [&](const char* name, SetFloat setter, GetFloat getter, float value) {
		Json::Value r;
		r["set"] = (int)(ov->*setter)(h, value);
		float got = -1;
		r["get"] = (int)(ov->*getter)(h, &got);
		r["value"] = got;
		r["expected"] = value;
		c.Record(name, r);
	};
	setGetFloat("Alpha", &IVROverlay::SetOverlayAlpha, &IVROverlay::GetOverlayAlpha, 0.5f);
	setGetFloat("WidthInMeters", &IVROverlay::SetOverlayWidthInMeters, &IVROverlay::GetOverlayWidthInMeters, 1.5f);
	setGetFloat("Curvature", &IVROverlay::SetOverlayCurvature, &IVROverlay::GetOverlayCurvature, 0.25f);
	setGetFloat("PreCurvePitch", &IVROverlay::SetOverlayPreCurvePitch, &IVROverlay::GetOverlayPreCurvePitch, 0.125f);
	setGetFloat("TexelAspect", &IVROverlay::SetOverlayTexelAspect, &IVROverlay::GetOverlayTexelAspect, 2.0f);
	{
		Json::Value r;
		r["set"] = (int)ov->SetOverlaySortOrder(h, 3);
		uint32_t got = 0;
		r["get"] = (int)ov->GetOverlaySortOrder(h, &got);
		r["value"] = got;
		r["expected"] = 3;
		c.Record("SortOrder", r);
	}
	{
		Json::Value r;
		r["set"] = (int)ov->SetOverlayRenderingPid(h, 1234);
		r["value"] = ov->GetOverlayRenderingPid(h);
		r["expected"] = 1234;
		c.Record("RenderingPid", r);
	}
	{
		Json::Value r;
		r["set"] = (int)ov->SetOverlayColor(h, 0.25f, 0.5f, 0.75f);
		float cr = 0, cg = 0, cb = 0;
		r["get"] = (int)ov->GetOverlayColor(h, &cr, &cg, &cb);
		r["value"] = cr + cg * 10 + cb * 100;
		r["expected"] = 0.25 + 5.0 + 75.0;
		c.Record("Color", r);
	}
	{
		Json::Value r;
		r["set"] = (int)ov->SetOverlayFlag(h, VROverlayFlags_SideBySide_Parallel, true);
		bool got = false;
		r["get"] = (int)ov->GetOverlayFlag(h, VROverlayFlags_SideBySide_Parallel, &got);
		r["value"] = got;
		r["expected"] = true;
		c.Record("Flag", r);
	}
	{
		vr::HmdMatrix34_t m{};
		m.m[0][0] = m.m[1][1] = m.m[2][2] = 1;
		m.m[1][3] = 1.25f;
		Json::Value r;
		r["set"] = (int)ov->SetOverlayTransformAbsolute(h, vr::TrackingUniverseStanding, &m);
		vr::HmdMatrix34_t got{};
		vr::ETrackingUniverseOrigin origin = vr::TrackingUniverseSeated;
		r["get"] = (int)ov->GetOverlayTransformAbsolute(h, &origin, &got);
		r["value"] = got.m[1][3];
		r["expected"] = 1.25f;
		c.Record("TransformAbsolute", r);
	}
	vr::Texture_t tex{};
	callers::Texture made(ArgSet::Valid, *c.call);
	c.Record("SetOverlayTexture", Err(made.get() ? (int)ov->SetOverlayTexture(h, made.get()) : -1));
	c.Record("ShowOverlay", Err(ov->ShowOverlay(h)));
	{
		Json::Value r;
		r["ret"] = ov->IsOverlayVisible(h);
		c.Record("IsOverlayVisible", r);
	}
	c.Record("HideOverlay", Err(ov->HideOverlay(h)));
	c.Record("DestroyOverlay", Err(ov->DestroyOverlay(h)));
	(void)tex;
	c.core->Cleanup();
}

void InputManifest(CaseContext& c)
{
	using namespace vr::IVRInput_010;
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le;
	auto* in = (IVRInput*)c.core->GetGenericInterface("IVRInput_010", &le);
	if (!in) {
		c.Record("no IVRInput_010", Err(le));
		c.core->Cleanup();
		return;
	}
	c.Record("SetActionManifestPath", Err(in->SetActionManifestPath(c.manifest.c_str())));
	vr::VRActionSetHandle_t set = vr::k_ulInvalidActionSetHandle;
	c.Record("GetActionSetHandle", Err(in->GetActionSetHandle("/actions/main", &set)));
	vr::VRActionHandle_t trigger = 0, grab = 0, pose = 0, haptic = 0;
	c.Record("GetActionHandle trigger", Err(in->GetActionHandle("/actions/main/in/trigger", &trigger)));
	c.Record("GetActionHandle grab", Err(in->GetActionHandle("/actions/main/in/grab", &grab)));
	c.Record("GetActionHandle pose", Err(in->GetActionHandle("/actions/main/in/pose", &pose)));
	c.Record("GetActionHandle haptic", Err(in->GetActionHandle("/actions/main/out/haptic", &haptic)));
	VRActiveActionSet_t active{};
	active.ulActionSet = set;
	c.Record("UpdateActionState", Err(in->UpdateActionState(&active, sizeof(active), 1)));
	InputDigitalActionData_t digital{};
	c.Record("GetDigitalActionData grab", Err(in->GetDigitalActionData(grab, &digital, sizeof(digital), vr::k_ulInvalidInputValueHandle)));
	InputAnalogActionData_t analog{};
	c.Record("GetAnalogActionData trigger", Err(in->GetAnalogActionData(trigger, &analog, sizeof(analog), vr::k_ulInvalidInputValueHandle)));
	InputPoseActionData_t poseData{};
	c.Record("GetPoseActionDataForNextFrame pose",
	    Err(in->GetPoseActionDataForNextFrame(pose, vr::TrackingUniverseStanding, &poseData, sizeof(poseData), vr::k_ulInvalidInputValueHandle)));
	c.Record("TriggerHapticVibrationAction", Err(in->TriggerHapticVibrationAction(haptic, 0, 0.1f, 160, 0.5f, vr::k_ulInvalidInputValueHandle)));
	c.core->Cleanup();
}

struct Frames {
	vr::IVRCompositor_029::IVRCompositor* comp = nullptr;
	vr::IVRSystem_022::IVRSystem* sys = nullptr;
};

bool OpenFrames(CaseContext& c, Frames& f)
{
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return false;
	}
	vr::EVRInitError le;
	f.comp = (vr::IVRCompositor_029::IVRCompositor*)c.core->GetGenericInterface("IVRCompositor_029", &le);
	f.sys = (vr::IVRSystem_022::IVRSystem*)c.core->GetGenericInterface("IVRSystem_022", &le);
	if (!f.comp || !f.sys) {
		c.Record("no compositor or system", Err(le));
		c.core->Cleanup();
		return false;
	}
	return true;
}

void FrameLoop(CaseContext& c)
{
	Frames f;
	if (!OpenFrames(c, f))
		return;
	callers::Texture tex(ArgSet::Valid, *c.call);
	for (int frame = 1; frame <= 120; frame++) {
		vr::TrackedDevicePose_t poses[16];
		Json::Value r;
		r["wait"] = (int)f.comp->WaitGetPoses(poses, 16, nullptr, 0);
		r["left"] = (int)f.comp->Submit(vr::Eye_Left, tex.get(), nullptr, vr::Submit_Default);
		r["right"] = (int)f.comp->Submit(vr::Eye_Right, tex.get(), nullptr, vr::Submit_Default);
		Json::Value events(Json::arrayValue);
		vr::VREvent_t ev{};
		for (int n = 0; n < 64 && f.sys->PollNextEvent(&ev, sizeof(ev)); n++)
			events.append((int)ev.eventType);
		r["events"] = events;
		c.Record("frame " + std::to_string(frame), r);
	}
	c.core->Cleanup();
}

// IVRSystem_003-010 poll into SDK 0.9.x's 32-byte VREvent_t, not today's 64-byte one. 120 frames as frame-loop, polling
// through IVRSystem_009 into a 64-byte buffer: each frame records its events and how many bytes past 32 were written
void OldEvents(CaseContext& c)
{
	Frames f;
	if (!OpenFrames(c, f))
		return;
	vr::EVRInitError le;
	auto* old = (vr::IVRSystem_009::IVRSystem*)c.core->GetGenericInterface("IVRSystem_009", &le);
	if (!old) {
		c.Record("no IVRSystem_009", Err(le));
		c.core->Cleanup();
		return;
	}
	callers::Texture tex(ArgSet::Valid, *c.call);
	for (int frame = 1; frame <= 120; frame++) {
		vr::TrackedDevicePose_t poses[16];
		f.comp->WaitGetPoses(poses, 16, nullptr, 0);
		f.comp->Submit(vr::Eye_Left, tex.get(), nullptr, vr::Submit_Default);
		f.comp->Submit(vr::Eye_Right, tex.get(), nullptr, vr::Submit_Default);
		Json::Value r;
		Json::Value events(Json::arrayValue);
		int overrun = 0;
		for (int n = 0; n < 64; n++) {
			alignas(8) uint8_t buffer[64];
			memset(buffer, 0xCD, sizeof(buffer));
			if (!old->PollNextEvent((vr::VREvent_t*)buffer))
				break;
			uint32_t type;
			memcpy(&type, buffer, sizeof(type));
			events.append((int)type);
			for (int i = 32; i < 64; i++)
				overrun += buffer[i] != 0xCD;
		}
		r["events"] = events;
		r["overrun"] = overrun;
		c.Record("frame " + std::to_string(frame), r);
	}
	c.core->Cleanup();
}

Json::Value PoseRecord(const vr::TrackedDevicePose_t& p)
{
	Json::Value r;
	for (int i = 0; i < 3; i++)
		r["position"].append(p.mDeviceToAbsoluteTracking.m[i][3]);
	for (int i = 0; i < 3; i++)
		r["velocity"].append(p.vVelocity.v[i]);
	for (int i = 0; i < 3; i++)
		r["angular_velocity"].append(p.vAngularVelocity.v[i]);
	r["valid"] = p.bPoseIsValid;
	return r;
}

// The old IVRSystem calls whose answers can be checked without frames (expect/old_calls.json)
void OldCalls(CaseContext& c)
{
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le;
	auto* sys = (vr::IVRSystem_022::IVRSystem*)c.core->GetGenericInterface("IVRSystem_022", &le);
	auto* sys007 = (vr::IVRSystem_007::IVRSystem*)c.core->GetGenericInterface("IVRSystem_007", &le);
	auto* sys003 = (vr::IVRSystem_003::IVRSystem*)c.core->GetGenericInterface("IVRSystem_003", &le);
	if (!sys || !sys007 || !sys003) {
		c.Record("no IVRSystem_022, 007 or 003", Err(le));
		c.core->Cleanup();
		return;
	}

	// A pose at (1, 2, 3) moving along +x and turning about +y, moved by a quarter turn about +y then 10 m along +x
	vr::TrackedDevicePose_t pose = {};
	pose.mDeviceToAbsoluteTracking = { { { 1, 0, 0, 1 }, { 0, 1, 0, 2 }, { 0, 0, 1, 3 } } };
	pose.vVelocity = { { 1, 0, 0 } };
	pose.vAngularVelocity = { { 0, 1, 0 } };
	pose.bPoseIsValid = true;
	vr::HmdMatrix34_t transform = { { { 0, 0, 1, 10 }, { 0, 1, 0, 0 }, { -1, 0, 0, 0 } } };
	vr::TrackedDevicePose_t out = {};
	sys->ApplyTransform(&out, &pose, &transform);
	c.Record("ApplyTransform", PoseRecord(out));
	vr::TrackedDevicePose_t byRef = {};
	sys007->ApplyTransform(&byRef, pose, transform);
	c.Record("ApplyTransform IVRSystem_007", PoseRecord(byRef));

	int32_t adapter = -2, output = -2;
	sys003->GetDXGIOutputInfo(&adapter, &output);
	Json::Value dxgi;
	dxgi["output"] = output;
	dxgi["adapter_set"] = adapter != -2;
	c.Record("GetDXGIOutputInfo IVRSystem_003", dxgi);

	Json::Value attach;
	attach["ret"] = sys003->AttachToWindow(nullptr);
	c.Record("AttachToWindow", attach);

	vr::IVRSystem_003::RenderModel_t model;
	memset(&model, 0xCD, sizeof(model));
	Json::Value load;
	load["ret"] = sys003->LoadRenderModel("renderLeftHand", &model);
	load["has_vertices"] = model.unVertexCount > 0 && model.rVertexData != nullptr;
	load["has_triangles"] = model.unTriangleCount > 0 && model.rIndexData != nullptr;
	load["texture"] = std::to_string(model.diffuseTexture.unWidth) + "x" + std::to_string(model.diffuseTexture.unHeight);
	load["has_texture_data"] = model.diffuseTexture.rubTextureMapData != nullptr;
	c.Record("LoadRenderModel renderLeftHand", load);
	sys003->FreeRenderModel(&model);
	Json::Value freed;
	freed["handle"] = (Json::UInt64)model.ulInternalHandle;
	freed["vertices"] = model.unVertexCount;
	c.Record("FreeRenderModel", freed);
	sys003->FreeRenderModel(&model); // again: ignored

	memset(&model, 0xCD, sizeof(model));
	Json::Value unknown;
	unknown["ret"] = sys003->LoadRenderModel("no such model", &model);
	unknown["handle"] = (Json::UInt64)model.ulInternalHandle;
	c.Record("LoadRenderModel unknown", unknown);

	c.core->Cleanup();
}

// The old compositors' Submit forms on a D3D11 texture (expect/old_submit.json): IVRCompositor_005 and 006 name the
// device with SetGraphicsDevice, 008 passes the API with each texture. And GetFrameTiming writes only the old struct.
void OldSubmit(CaseContext& c)
{
	Frames f;
	if (!OpenFrames(c, f))
		return;
	vr::EVRInitError le;
	auto* c005 = (vr::IVRCompositor_005::IVRCompositor*)c.core->GetGenericInterface("IVRCompositor_005", &le);
	auto* c006 = (vr::IVRCompositor_006::IVRCompositor*)c.core->GetGenericInterface("IVRCompositor_006", &le);
	auto* c008 = (vr::IVRCompositor_008::IVRCompositor*)c.core->GetGenericInterface("IVRCompositor_008", &le);
	if (!c005 || !c006 || !c008) {
		c.Record("no IVRCompositor_005, 006 or 008", Err(le));
		c.core->Cleanup();
		return;
	}
	callers::Texture tex(ArgSet::Valid, *c.call);
	void* handle = tex.get() ? tex.get()->handle : nullptr;
	vr::TrackedDevicePose_t poses[16];

	// 008: the API with each texture
	Json::Value r;
	r["wait"] = (int)c008->WaitGetPoses(poses, 16, nullptr, 0);
	r["left"] = (int)c008->Submit(vr::Eye_Left, API_DirectX, handle, nullptr, vr::Submit_Default);
	r["right"] = (int)c008->Submit(vr::Eye_Right, API_DirectX, handle, nullptr, vr::Submit_Default);
	c.Record("IVRCompositor_008 frame", r);

	// 006: no device named yet, so the texture can't be read; then D3D11
	r = Json::Value();
	r["wait"] = (int)c006->WaitGetPoses(poses, 16, nullptr, 0);
	r["left before SetGraphicsDevice"] = (int)c006->Submit(vr::Eye_Left, handle, nullptr);
	c006->SetGraphicsDevice(vr::IVRCompositor_006::Compositor_DeviceType_D3D11, c.call->d3d11);
	r["left"] = (int)c006->Submit(vr::Eye_Left, handle, nullptr);
	r["right"] = (int)c006->Submit(vr::Eye_Right, handle, nullptr);
	c.Record("IVRCompositor_006 frame", r);

	// 005: the same, with its own bounds struct and no answers; a frame after it still works
	c005->SetGraphicsDevice(vr::IVRCompositor_005::Compositor_DeviceType_D3D11, c.call->d3d11);
	c005->WaitGetPoses(poses, 16);
	vr::IVRCompositor_005::Compositor_TextureBounds bounds = { 0, 0, 1, 1 };
	c005->Submit(vr::Eye_Left, handle, &bounds);
	c005->Submit(vr::Eye_Right, handle, &bounds);
	r = Json::Value();
	r["wait after"] = (int)f.comp->WaitGetPoses(poses, 16, nullptr, 0);
	r["left after"] = (int)f.comp->Submit(vr::Eye_Left, tex.get(), nullptr, vr::Submit_Default);
	c.Record("IVRCompositor_005 frame", r);

	// Frame timing in 005's and 008's own layouts: nothing written past the struct
	alignas(8) uint8_t buffer[512];
	memset(buffer, 0xCD, sizeof(buffer));
	auto* t005 = (vr::IVRCompositor_005::Compositor_FrameTiming*)buffer;
	t005->size = sizeof(*t005);
	r = Json::Value();
	r["ret"] = c005->GetFrameTiming(t005, 0);
	int overrun = 0;
	for (size_t i = sizeof(*t005); i < sizeof(buffer); i++)
		overrun += buffer[i] != 0xCD;
	r["overrun"] = overrun;
	r["size kept"] = t005->size == sizeof(*t005);
	c.Record("IVRCompositor_005 GetFrameTiming", r);

	memset(buffer, 0xCD, sizeof(buffer));
	auto* t008 = (vr::IVRCompositor_008::Compositor_FrameTiming*)buffer;
	t008->size = sizeof(*t008);
	r = Json::Value();
	r["ret"] = c008->GetFrameTiming(t008, 0);
	overrun = 0;
	for (size_t i = sizeof(*t008); i < sizeof(buffer); i++)
		overrun += buffer[i] != 0xCD;
	r["overrun"] = overrun;
	r["size kept"] = t008->size == sizeof(*t008);
	c.Record("IVRCompositor_008 GetFrameTiming", r);

	c.core->Cleanup();
}

// An event polled into SDK 0.9.x's 32-byte VREvent_t: its type, the keyboard user value at its data's offset 8 (16), and
// how many bytes past 32 were written
Json::Value OldEventRecord(bool polled, const uint8_t* buffer)
{
	Json::Value r;
	r["polled"] = polled;
	uint32_t type;
	uint64_t userValue;
	memcpy(&type, buffer, sizeof(type));
	memcpy(&userValue, buffer + 16, sizeof(userValue));
	r["type"] = (int)type;
	r["user value"] = (Json::UInt64)userValue;
	int overrun = 0;
	for (int i = 32; i < 64; i++)
		overrun += buffer[i] != 0xCD;
	r["overrun"] = overrun;
	return r;
}

// The old overlay calls (expect/old_overlay.json): events in the old layout, flags as bit numbers (up to 020) and as
// masks (021 on), the 0.9.x gamma and visibility calls, and 001's texture of the device SetGraphicsDevice named
void OldOverlay(CaseContext& c)
{
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le;
	auto* ov = (vr::IVROverlay_028::IVROverlay*)c.core->GetGenericInterface("IVROverlay_028", &le);
	auto* ov007 = (vr::IVROverlay_007::IVROverlay*)c.core->GetGenericInterface("IVROverlay_007", &le);
	auto* ov006 = (vr::IVROverlay_006::IVROverlay*)c.core->GetGenericInterface("IVROverlay_006", &le);
	auto* ov001 = (vr::IVROverlay_001::IVROverlay*)c.core->GetGenericInterface("IVROverlay_001", &le);
	auto* sys009 = (vr::IVRSystem_009::IVRSystem*)c.core->GetGenericInterface("IVRSystem_009", &le);
	if (!ov || !ov007 || !ov006 || !ov001 || !sys009) {
		c.Record("no IVROverlay_028, 007, 006, 001 or IVRSystem_009", Err(le));
		c.core->Cleanup();
		return;
	}
	vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
	c.Record("CreateOverlay", Err(ov->CreateOverlay("oc.old", "Old", &h)));

	// The keyboard sends KeyboardDone at once: polled through 007 (the overlay's) and IVRSystem_009 (the system's)
	alignas(8) uint8_t buffer[64];
	ov007->ShowKeyboardForOverlay(h, vr::IVROverlay_007::k_EGamepadTextInputModeNormal, vr::IVROverlay_007::k_EGamepadTextInputLineModeSingleLine, "", 8, "",
	    false, 0x1122334455667788ull);
	memset(buffer, 0xCD, sizeof(buffer));
	c.Record("IVROverlay_007 PollNextOverlayEvent", OldEventRecord(ov007->PollNextOverlayEvent(h, (vr::VREvent_t*)buffer), buffer));
	ov007->HideKeyboard();
	ov007->ShowKeyboard(vr::IVROverlay_007::k_EGamepadTextInputModeNormal, vr::IVROverlay_007::k_EGamepadTextInputLineModeSingleLine, "", 8, "", false, 0x55aaull);
	memset(buffer, 0xCD, sizeof(buffer));
	c.Record("IVRSystem_009 PollNextEvent", OldEventRecord(sys009->PollNextEvent((vr::VREvent_t*)buffer), buffer));
	ov007->HideKeyboard();

	// Flags: 007 passes NoDashboardTab as 3, 028 as 1 << 3; both are bit 3 of GetOverlayFlags
	Json::Value flags;
	flags["set 007"] = (int)ov007->SetOverlayFlag(h, (vr::IVROverlay_007::VROverlayFlags)3, true);
	uint32_t all = 0;
	ov->GetOverlayFlags(h, &all);
	flags["after 007"] = all;
	ov007->SetOverlayFlag(h, (vr::IVROverlay_007::VROverlayFlags)3, false);
	flags["set 028"] = (int)ov->SetOverlayFlag(h, vr::IVROverlay_028::VROverlayFlags_SideBySide_Parallel, true);
	bool enabled = false;
	ov->GetOverlayFlag(h, vr::IVROverlay_028::VROverlayFlags_SideBySide_Parallel, &enabled);
	flags["028 enabled"] = enabled;
	all = 0;
	ov->GetOverlayFlags(h, &all);
	flags["after 028"] = all;
	enabled = false;
	ov007->GetOverlayFlag(h, (vr::IVROverlay_007::VROverlayFlags)10, &enabled); // SideBySide_Parallel's bit number
	flags["007 sees it"] = enabled;
	c.Record("flags", flags);

	Json::Value gamma;
	gamma["set"] = (int)ov006->SetOverlayGamma(h, 0.5f);
	float got = -1;
	gamma["get"] = (int)ov006->GetOverlayGamma(h, &got);
	gamma["value"] = got;
	c.Record("IVROverlay_006 gamma", gamma);

	Json::Value vis;
	vis["set"] = (int)ov001->SetOverlayVisibility(h, vr::IVROverlay_001::VROverlayVisibility_SystemOverlay);
	vr::IVROverlay_001::VROverlayVisibility v = vr::IVROverlay_001::VROverlayVisibility_Manual;
	vis["get"] = (int)ov001->GetOverlayVisibility(h, &v);
	vis["value"] = (int)v;
	vis["system overlay visible"] = ov001->IsSystemOverlayVisible();
	c.Record("IVROverlay_001 visibility", vis);

	// No IVRCompositor::SetGraphicsDevice: 001 can't tell what the texture is
	Json::Value tex;
	tex["ret"] = (int)ov001->SetOverlayTexture(h, nullptr);
	c.Record("IVROverlay_001 SetOverlayTexture", tex);

	ov->DestroyOverlay(h);
	c.core->Cleanup();
}

// The remaining old versions (expect/old_misc.json): IVRApplications_001, IVRChaperone_002, IVRRenderModels_001,
// IVRNotifications_001, IVRControlPanel_001 and IVRInput_003
void OldMisc(CaseContext& c)
{
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le;
	auto* apps = (vr::IVRApplications_001::IVRApplications*)c.core->GetGenericInterface("IVRApplications_001", &le);
	auto* chap = (vr::IVRChaperone_002::IVRChaperone*)c.core->GetGenericInterface("IVRChaperone_002", &le);
	auto* chap4 = (vr::IVRChaperone_004::IVRChaperone*)c.core->GetGenericInterface("IVRChaperone_004", &le);
	auto* models = (vr::IVRRenderModels_001::IVRRenderModels*)c.core->GetGenericInterface("IVRRenderModels_001", &le);
	auto* notes = (vr::IVRNotifications_001::IVRNotifications*)c.core->GetGenericInterface("IVRNotifications_001", &le);
	auto* panel = (vr::IVRControlPanel_001::IVRControlPanel*)c.core->GetGenericInterface("IVRControlPanel_001", &le);
	auto* input = (vr::IVRInput_003::IVRInput*)c.core->GetGenericInterface("IVRInput_003", &le);
	void* sys = c.core->GetGenericInterface("IVRSystem_022", &le);
	if (!apps || !chap || !chap4 || !models || !notes || !panel || !input || !sys) {
		c.Record("an old interface is missing", Err(le));
		c.core->Cleanup();
		return;
	}

	Json::Value r;
	char key[64] = "x";
	r["ret"] = (int)apps->GetHomeApplication(key, sizeof(key));
	r["key"] = key;
	c.Record("IVRApplications_001 GetHomeApplication", r);

	r = Json::Value();
	vr::IVRChaperone_002::ChaperoneSoftBoundsInfo_t soft = {};
	vr::HmdQuad_t rect = {};
	r["ret"] = chap->GetSoftBoundsInfo(&soft);
	r["play area ret"] = chap4->GetPlayAreaRect(&rect);
	r["same as play area"] = memcmp(&soft.quadCorners, &rect, sizeof(rect)) == 0;
	vr::IVRChaperone_002::ChaperoneSeatedBoundsInfo_t seated;
	memset(&seated, 0xCD, sizeof(seated));
	r["seated ret"] = chap->GetSeatedBoundsInfo(&seated);
	r["seated zeroed"] = seated.vSeatedHeadPosition.v[0] == 0.0f;
	c.Record("IVRChaperone_002 bounds", r);

	r = Json::Value();
	vr::IVRRenderModels_001::RenderModel_t model;
	memset(&model, 0xCD, sizeof(model));
	r["ret"] = models->LoadRenderModel("renderLeftHand", &model);
	r["has_vertices"] = model.unVertexCount > 0 && model.rVertexData != nullptr;
	r["texture"] = std::to_string(model.diffuseTexture.unWidth) + "x" + std::to_string(model.diffuseTexture.unHeight);
	models->FreeRenderModel(&model);
	r["freed handle"] = (Json::UInt64)model.ulInternalHandle;
	c.Record("IVRRenderModels_001 LoadRenderModel", r);

	r = Json::Value();
	char name[64] = {};
	r["size"] = notes->GetErrorString(vr::IVRNotifications_001::k_ENotificationError_Fail, name, sizeof(name));
	r["name"] = name;
	vr::IVRNotifications_001::VRNotificationId id = 0;
	r["create"] = (int)notes->CreateNotification(vr::k_ulOverlayHandleInvalid, 0, "type", "text", "category", nullptr, &id);
	r["id set"] = id != 0;
	r["dismiss"] = (int)notes->DismissNotification(id);
	c.Record("IVRNotifications_001", r);

	r = Json::Value();
	r["drivers"] = panel->GetDriverCount();
	r["display interface"] = (void*)panel->GetCurrentDisplayInterface("IVRSystem_022") == sys;
	r["unknown interface null"] = panel->GetCurrentDisplayInterface("IVRNoSuchThing_001") == nullptr;
	r["ipd positive"] = panel->GetIPD() > 0.0f;
	c.Record("IVRControlPanel_001", r);

	// IVRInput_003's action sets are 24 bytes (today's 32)
	r = Json::Value();
	vr::IVRInput_003::VRActiveActionSet_t set = {};
	r["24 bytes"] = (int)input->UpdateActionState(&set, 24, 0);
	r["32 bytes"] = (int)input->UpdateActionState(&set, 32, 0);
	c.Record("IVRInput_003 UpdateActionState", r);

	c.core->Cleanup();
}

// The interfaces the fork never had (expect/new_interfaces.json): an anchor kept and given back through its descriptor,
// the one driver (the OpenXR runtime), and no camera
void NewInterfaces(CaseContext& c)
{
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le;
	auto* anchors = (vr::IVRSpatialAnchors_001::IVRSpatialAnchors*)c.core->GetGenericInterface("IVRSpatialAnchors_001", &le);
	auto* drivers = (vr::IVRDriverManager_001::IVRDriverManager*)c.core->GetGenericInterface("IVRDriverManager_001", &le);
	auto* camera = (vr::IVRTrackedCamera_006::IVRTrackedCamera*)c.core->GetGenericInterface("IVRTrackedCamera_006", &le);
	if (!anchors || !drivers || !camera) {
		c.Record("a new interface is missing", Err(le));
		c.core->Cleanup();
		return;
	}

	vr::IVRSpatialAnchors_001::SpatialAnchorPose_t pose = { { { { 1, 0, 0, 0.5f }, { 0, 1, 0, 1.25f }, { 0, 0, 1, -2 } } } };
	vr::SpatialAnchorHandle_t handle = 0;
	Json::Value r;
	r["create"] = (int)anchors->CreateSpatialAnchorFromPose(0, vr::TrackingUniverseStanding, &pose, &handle);
	vr::IVRSpatialAnchors_001::SpatialAnchorPose_t back = {};
	r["get"] = (int)anchors->GetSpatialAnchorPose(handle, vr::TrackingUniverseStanding, &back);
	r["same pose"] = memcmp(&back, &pose, sizeof(pose)) == 0;
	uint32_t length = 0;
	r["descriptor size asked"] = (int)anchors->GetSpatialAnchorDescriptor(handle, nullptr, &length);
	std::string descriptor(length, '\0');
	r["descriptor"] = (int)anchors->GetSpatialAnchorDescriptor(handle, descriptor.data(), &length);
	vr::SpatialAnchorHandle_t again = 0;
	r["from descriptor"] = (int)anchors->CreateSpatialAnchorFromDescriptor(descriptor.c_str(), &again);
	back = {};
	anchors->GetSpatialAnchorPose(again, vr::TrackingUniverseStanding, &back);
	r["descriptor pose same"] = memcmp(&back, &pose, sizeof(pose)) == 0;
	r["new handle"] = again != handle && again != 0;
	r["unknown handle"] = (int)anchors->GetSpatialAnchorPose(handle + 1000, vr::TrackingUniverseStanding, &back);
	c.Record("IVRSpatialAnchors_001 round trip", r);

	r = Json::Value();
	char name[64] = {};
	r["count"] = drivers->GetDriverCount();
	r["name size"] = drivers->GetDriverName(0, name, sizeof(name));
	r["name"] = name;
	r["handle"] = (Json::UInt64)drivers->GetDriverHandle(name);
	r["enabled"] = drivers->IsEnabled(0);
	c.Record("IVRDriverManager_001", r);

	r = Json::Value();
	bool has = true;
	r["ret"] = (int)camera->HasCamera(vr::k_unTrackedDeviceIndex_Hmd, &has);
	r["has camera"] = has;
	c.Record("IVRTrackedCamera_006 HasCamera", r);

	// IVRPaths through the C API, as Valve offers it: openvr_capi.h's VR_IVRPaths_FnTable
#ifdef _WIN32
#define OC_FNTABLE __stdcall
#else
#define OC_FNTABLE
#endif
	using namespace vr::IVRPaths_002;
	struct PathsFnTable {
		vr::ETrackedPropertyError(OC_FNTABLE* ReadPathBatch)(vr::PropertyContainerHandle_t, PathRead_t*, uint32_t);
		vr::ETrackedPropertyError(OC_FNTABLE* WritePathBatch)(vr::PropertyContainerHandle_t, PathWrite_t*, uint32_t);
		vr::ETrackedPropertyError(OC_FNTABLE* StringToHandle)(PathHandle_t*, const char*);
		vr::ETrackedPropertyError(OC_FNTABLE* HandleToString)(PathHandle_t, char*, uint32_t, uint32_t*);
	};
#undef OC_FNTABLE
	auto* paths = (PathsFnTable*)c.core->GetGenericInterface("FnTable:IVRPaths_002", &le);
	r = Json::Value();
	r["found"] = paths != nullptr;
	if (paths) {
		PathHandle_t first = 0, second = 0;
		r["to handle"] = (int)paths->StringToHandle(&first, "/user/head/proximity");
		paths->StringToHandle(&second, "/user/head/proximity");
		r["same handle"] = first == second && first != 0;
		char path[64] = {};
		uint32_t used = 0;
		r["to string"] = (int)paths->HandleToString(first, path, sizeof(path), &used);
		r["path"] = path;
		r["used"] = used;
		r["small buffer"] = (int)paths->HandleToString(first, path, 4, &used);
		PathRead_t read[2] = {};
		read[0].ulPath = first;
		read[1].ulPath = first;
		r["read"] = (int)paths->ReadPathBatch(0, read, 2);
		r["entry errors"] = (int)read[0].eError * 10 + (int)read[1].eError;
		PathWrite_t write[2] = {};
		write[1].bValueChanged = true; // 002's last field, past 001's struct: the stride is 002's
		r["write"] = (int)paths->WritePathBatch(0, write, 2);
		r["write entry errors"] = (int)write[0].eError * 10 + (int)write[1].eError;
		r["stride kept"] = write[1].bValueChanged;
	}
	c.Record("FnTable:IVRPaths_002", r);

	c.core->Cleanup();
}

// What the devices say they are (expect/device_identity.json): the HMD's row in the identity table, a string answer
// that doesn't fit, and IVRSystem_003's distortion
void DeviceIdentityCase(CaseContext& c)
{
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le;
	auto* sys = (vr::IVRSystem_022::IVRSystem*)c.core->GetGenericInterface("IVRSystem_022", &le);
	auto* sys003 = (vr::IVRSystem_003::IVRSystem*)c.core->GetGenericInterface("IVRSystem_003", &le);
	if (!sys || !sys003) {
		c.Record("no IVRSystem_022 or 003", Err(le));
		c.core->Cleanup();
		return;
	}
	auto stringProp = [&](const char* label, vr::ETrackedDeviceProperty prop, uint32_t size) {
		char value[64];
		memset(value, 'x', sizeof(value));
		vr::ETrackedPropertyError err = vr::TrackedProp_Success;
		Json::Value r;
		r["ret"] = sys->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, prop, value, size, &err);
		r["error"] = (int)err;
		r["value"] = size ? std::string(value, strnlen(value, size)) : std::string();
		c.Record(label, r);
	};
	stringProp("HMD TrackingSystemName", vr::Prop_TrackingSystemName_String, 64);
	stringProp("HMD ManufacturerName", vr::Prop_ManufacturerName_String, 64);
	stringProp("HMD RenderModelName", vr::Prop_RenderModelName_String, 64);
	stringProp("HMD ControllerType", vr::Prop_ControllerType_String, 64);
	stringProp("HMD TrackingSystemName in 3 bytes", vr::Prop_TrackingSystemName_String, 3);

	Json::Value r;
	vr::DistortionCoordinates_t d = sys003->ComputeDistortion(vr::Eye_Left, 0.25f, 0.75f);
	r["red"] = std::to_string(d.rfRed[0]) + "," + std::to_string(d.rfRed[1]);
	r["blue"] = std::to_string(d.rfBlue[0]) + "," + std::to_string(d.rfBlue[1]);
	c.Record("IVRSystem_003 ComputeDistortion", r);

	c.core->Cleanup();
}

// This program's folder, where oc_echo_app is built
std::filesystem::path OwnFolder()
{
#ifdef _WIN32
	wchar_t path[MAX_PATH * 4];
	DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof(path) / sizeof(path[0])));
	return std::filesystem::path(std::wstring(path, n)).parent_path();
#else
	std::error_code ec;
	return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

std::string JsonEscaped(const std::string& s)
{
	std::string out;
	for (char ch : s) {
		if (ch == '\\' || ch == '"')
			out += '\\';
		out += ch;
	}
	return out;
}

// The application registry (expect/applications.json): a manifest added, its application installed, launched
// (oc_echo_app writes a marker file) and named. A Utility app, so it runs without an OpenXR runtime too.
void ApplicationsCase(CaseContext& c)
{
	if (c.core->Init(vr::VRApplication_Utility, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le;
	auto* apps = (vr::IVRApplications_007::IVRApplications*)c.core->GetGenericInterface("IVRApplications_007", &le);
	if (!apps) {
		c.Record("no IVRApplications_007", Err(le));
		c.core->Cleanup();
		return;
	}

	// echo.vrmanifest, with the binary where it was built and the marker to write as its argument
	namespace fs = std::filesystem;
	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	fs::path dir = fs::temp_directory_path() / ("oc_applications_case_" + std::to_string(stamp));
	fs::create_directories(dir);
	fs::path marker = dir / "marker.txt";
	std::ifstream in(c.call->dataDir / "echo.vrmanifest");
	std::stringstream text;
	text << in.rdbuf();
	std::string manifest = text.str();
#ifdef _WIN32
	const char* binaryKey = "\"binary_path_windows\" : \"oc_echo_app.exe\"";
	fs::path binary = OwnFolder() / "oc_echo_app.exe";
#else
	const char* binaryKey = "\"binary_path_linux\" : \"oc_echo_app\"";
	fs::path binary = OwnFolder() / "oc_echo_app";
#endif
	auto replace = [&](const std::string& from, const std::string& to) {
		size_t at = manifest.find(from);
		if (at != std::string::npos)
			manifest.replace(at, from.size(), to);
	};
	std::string key = binaryKey;
	replace(key, key.substr(0, key.find(':') + 2) + "\"" + JsonEscaped(binary.generic_string()) + "\"");
	replace("\"arguments\" : \"\"", "\"arguments\" : \"\\\"" + JsonEscaped(marker.generic_string()) + "\\\" launched\"");
	fs::path manifestPath = dir / "echo.vrmanifest";
	std::ofstream(manifestPath) << manifest;

	c.Record("AddApplicationManifest", Err(apps->AddApplicationManifest(manifestPath.string().c_str(), false)));
	Json::Value installed;
	installed["ret"] = apps->IsApplicationInstalled("oc.test.echo");
	c.Record("IsApplicationInstalled", installed);

	Json::Value launch = Err(apps->LaunchApplication("oc.test.echo"));
	bool appeared = false;
	for (int i = 0; i < 50 && !appeared; i++) {
		std::error_code ec;
		appeared = fs::exists(marker, ec) && fs::file_size(marker, ec) > 0;
		if (!appeared)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	launch["marker"] = appeared;
	if (appeared) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100)); // its one line written
		std::ifstream m(marker);
		std::string line;
		std::getline(m, line);
		launch["marker_text"] = line;
	}
	c.Record("LaunchApplication", launch);

	char name[64] = "unchanged";
	vr::IVRApplications_007::EVRApplicationError error = vr::IVRApplications_007::VRApplicationError_None;
	Json::Value prop;
	prop["ret"] = apps->GetApplicationPropertyString("oc.test.echo", vr::IVRApplications_007::VRApplicationProperty_Name_String, name, sizeof(name), &error);
	prop["error"] = (int)error;
	prop["value"] = std::string(name);
	c.Record("GetApplicationPropertyString Name", prop);

	Json::Value last;
	last["after_launch"] = apps->GetApplicationPropertyUint64("oc.test.echo", vr::IVRApplications_007::VRApplicationProperty_LastLaunchTime_Uint64, &error) > 0;
	c.Record("LastLaunchTime", last);

	c.Record("RemoveApplicationManifest", Err(apps->RemoveApplicationManifest(manifestPath.string().c_str())));
	Json::Value gone;
	gone["ret"] = apps->IsApplicationInstalled("oc.test.echo");
	c.Record("IsApplicationInstalled after removal", gone);

	c.core->Cleanup();
	std::error_code ec;
	fs::remove_all(dir, ec);
}

void SubmitEdges(CaseContext& c)
{
	Frames f;
	if (!OpenFrames(c, f))
		return;
	callers::Texture tex(ArgSet::Valid, *c.call);
	vr::TrackedDevicePose_t poses[16];
	f.comp->WaitGetPoses(poses, 16, nullptr, 0);
	c.Record("left", Err(f.comp->Submit(vr::Eye_Left, tex.get())));
	c.Record("left again", Err(f.comp->Submit(vr::Eye_Left, tex.get())));
	c.Record("right", Err(f.comp->Submit(vr::Eye_Right, tex.get())));

	f.comp->WaitGetPoses(poses, 16, nullptr, 0);
	vr::Texture_t nullTex{ nullptr, vr::TextureType_DirectX, vr::ColorSpace_Auto };
	c.Record("left null", Err(f.comp->Submit(vr::Eye_Left, &nullTex)));
	c.Record("right not null", Err(f.comp->Submit(vr::Eye_Right, tex.get())));

#ifdef _WIN32
	f.comp->WaitGetPoses(poses, 16, nullptr, 0);
	vr::Texture_t odd{ nullptr, vr::TextureType_DirectX, vr::ColorSpace_Auto };
	if (c.call->d3d11) {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 256;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = (DXGI_FORMAT)78;
		desc.SampleDesc.Count = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ID3D11Texture2D* t = nullptr;
		if (SUCCEEDED(c.call->d3d11->CreateTexture2D(&desc, nullptr, &t)))
			odd.handle = t;
	}
	Json::Value r = Err(odd.handle ? (int)f.comp->Submit(vr::Eye_Left, &odd) : -1);
	r["texture_made"] = odd.handle != nullptr;
	c.Record("left DXGI format 78", r);
	f.comp->Submit(vr::Eye_Right, tex.get());
	if (odd.handle)
		((ID3D11Texture2D*)odd.handle)->Release();
#endif

	vr::Texture_t six[6];
	for (vr::Texture_t& t : six)
		t = *tex.get();
	c.Record("SetSkyboxOverride six faces", Err(f.comp->SetSkyboxOverride(six, 6)));
	c.core->Cleanup();

	// A Background app submitting (refused at Init on Windows, allowed on Linux for Proton)
	vr::EVRInitError e = c.core->Init(vr::VRApplication_Background, nullptr);
	Json::Value bg;
	bg["init_error"] = (int)e;
	if (e == vr::VRInitError_None) {
		vr::EVRInitError le;
		auto* comp = (vr::IVRCompositor_029::IVRCompositor*)c.core->GetGenericInterface("IVRCompositor_029", &le);
		bg["ret"] = comp ? (int)comp->Submit(vr::Eye_Left, tex.get()) : -1;
		c.core->Cleanup();
	}
	c.Record("Background submit", bg);
}

} // namespace

const std::map<std::string, CaseFn>& conformance::Cases()
{
	static const std::map<std::string, CaseFn> cases = {
		{ "init-matrix", InitMatrix },
		{ "init-repeat", InitRepeat },
		{ "init-other-thread", InitOtherThread },
		{ "lookup-before-init", LookupBeforeInit },
		{ "unknown-interface", UnknownInterface },
		{ "overlay-lifecycle", OverlayLifecycle },
		{ "input-manifest", InputManifest },
		{ "frame-loop", FrameLoop },
		{ "old-events", OldEvents },
		{ "old-calls", OldCalls },
		{ "old-submit", OldSubmit },
		{ "old-overlay", OldOverlay },
		{ "old-misc", OldMisc },
		{ "new-interfaces", NewInterfaces },
		{ "device-identity", DeviceIdentityCase },
		{ "applications", ApplicationsCase },
		{ "submit-edges", SubmitEdges },
	};
	return cases;
}
