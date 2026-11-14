// The frame cases, gfx-<api>-<what>: a game on one graphics API (gfx/GfxApi.h) submits the test pattern, and the
// stand-in runtime's dump shows what OpenComposite made of it. Each records "frame" with "dump_join":
// "last_projection", for which run.py joins the dump's last projection layer (per view: samples, rect, format,
// invalid) as the record's "dump"; a case whose API can't be made here records {"skipped": why}.

#include "conformance.h"

#include "gfx/GfxApi.h"

#include "IVRCompositor_029.h"

using namespace conformance;

std::unique_ptr<GfxApi> GfxApi::Create(GfxKind kind, std::string* whyNot)
{
#ifdef _WIN32
	switch (kind) {
	case GfxKind::D3D11:
		return MakeD3D11Gfx(whyNot);
	case GfxKind::D3D12:
		return MakeD3D12Gfx(whyNot);
	case GfxKind::Vulkan:
		return MakeVkGfx(whyNot);
	case GfxKind::OpenGL:
		return MakeGLGfx(whyNot);
	case GfxKind::D3D10:
		return MakeD3D10Gfx(whyNot);
	}
	*whyNot = "unknown graphics API";
	return nullptr;
#else
	(void)kind;
	*whyNot = "the frame cases' graphics are Windows only";
	return nullptr;
#endif
}

namespace {

// Formats in each API's own enum: RGBA8 sRGB
constexpr int64_t kDxgiRGBA8Srgb = 29; // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
constexpr int64_t kVkRGBA8Srgb = 43; // VK_FORMAT_R8G8B8A8_SRGB
constexpr int64_t kGlSRGB8Alpha8 = 0x8C43; // GL_SRGB8_ALPHA8

// The frames each case submits: enough for OpenComposite to move its session onto the game's device
constexpr int kFrames = 10;

Json::Value Skipped(const std::string& why)
{
	Json::Value r;
	r["skipped"] = why;
	return r;
}

// 1024x1024 per eye, full bounds, kFrames frames
void Basic(CaseContext& c, GfxKind kind, int64_t format)
{
	std::string why;
	std::unique_ptr<GfxApi> gfx = GfxApi::Create(kind, &why);
	if (!gfx) {
		c.Record("frame", Skipped(why));
		return;
	}
	GfxTexture left = gfx->Pattern(1024, 1024, format, 0);
	GfxTexture right = gfx->Pattern(1024, 1024, format, 1);
	gfx->Flush();
	if (!left.Made() || !right.Made()) {
		Json::Value r;
		r["error"] = "the pattern textures couldn't be made";
		c.Record("frame", r);
		return;
	}
	if (c.core->Init(vr::VRApplication_Scene, nullptr) != vr::VRInitError_None) {
		c.Record("Init failed");
		return;
	}
	vr::EVRInitError le = vr::VRInitError_None;
	auto* comp = (vr::IVRCompositor_029::IVRCompositor*)c.core->GetGenericInterface("IVRCompositor_029", &le);
	if (!comp) {
		Json::Value r;
		r["lookup_error"] = (int)le;
		c.Record("no compositor", r);
		c.core->Cleanup();
		return;
	}
	vr::VRTextureBounds_t bounds{ 0, 0, 1, 1 };
	Json::Value r;
	for (int frame = 1; frame <= kFrames; frame++) {
		vr::TrackedDevicePose_t poses[16];
		r["wait"] = (int)comp->WaitGetPoses(poses, 16, nullptr, 0);
		r["left"] = (int)comp->Submit(vr::Eye_Left, left.Texture(), &bounds, vr::Submit_Default);
		r["right"] = (int)comp->Submit(vr::Eye_Right, right.Texture(), &bounds, vr::Submit_Default);
	}
	r["frames"] = kFrames;
	r["dump_join"] = "last_projection";
	c.Record("frame", r);
	// OpenComposite first, then the textures and the device (OpenVR's rule for every API)
	c.core->Cleanup();
	gfx->Flush();
}

} // namespace

const std::map<std::string, CaseFn>& conformance::GfxCases()
{
	static const std::map<std::string, CaseFn> cases = {
		{ "gfx-d3d11-basic", [](CaseContext& c) { Basic(c, GfxKind::D3D11, kDxgiRGBA8Srgb); } },
		{ "gfx-d3d12-basic", [](CaseContext& c) { Basic(c, GfxKind::D3D12, kDxgiRGBA8Srgb); } },
		{ "gfx-vk-basic", [](CaseContext& c) { Basic(c, GfxKind::Vulkan, kVkRGBA8Srgb); } },
		{ "gfx-gl-basic", [](CaseContext& c) { Basic(c, GfxKind::OpenGL, kGlSRGB8Alpha8); } },
	};
	return cases;
}
