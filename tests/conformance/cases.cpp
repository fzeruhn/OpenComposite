// The named cases of oc_conformance: sequences the per-method callers can't do (real handles, frames, several Init
// rounds). Each writes one record per step: {"case", "method": <step label>, ...}.

#include "conformance.h"

#include "IVRCompositor_005.h"
#include "IVRCompositor_006.h"
#include "IVRCompositor_008.h"
#include "IVRCompositor_029.h"
#include "IVRInput_010.h"
#include "IVROverlay_028.h"
#include "IVRSystem_003.h"
#include "IVRSystem_007.h"
#include "IVRSystem_009.h"
#include "IVRSystem_022.h"

#include <cstring>

#include <thread>

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
		{ "submit-edges", SubmitEdges },
	};
	return cases;
}
