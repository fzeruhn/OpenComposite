// The stand-in runtime's OpenGL sessions (XR_KHR_opengl_enable, WGL): swapchain textures made in the app's context,
// their stored texels read back through a framebuffer and glReadPixels in it. The context is made current for that
// when it isn't.

#include "runtime_internal.h"

#include <algorithm>
#include <cstring>

namespace {

// GL 1.2 to 4.3 names and functions the 1.1 headers lack
constexpr GLenum kTexture2DArray = 0x8C1A, kTextureBinding2DArray = 0x8C1D, kTexture2DMultisample = 0x9100,
                 kTextureBinding2DMultisample = 0x9104, kTextureCubeMap = 0x8513, kTextureBindingCubeMap = 0x8514,
                 kCubeMapPositiveX = 0x8515, kReadFramebuffer = 0x8CA8, kReadFramebufferBinding = 0x8CAA,
                 kColorAttachment0 = 0x8CE0, kFramebufferComplete = 0x8CD5, kPixelPackBuffer = 0x88EB,
                 kPixelPackBufferBinding = 0x88ED, kHalfFloat = 0x140B, kUnsignedInt2101010Rev = 0x8368;
constexpr int64_t kSRGB8Alpha8 = 0x8C43, kRGBA8 = 0x8058, kRGBA16F = 0x881A, kRGB10A2 = 0x8059, kDepth32F = 0x8CAC,
                  kDepth24 = 0x81A6, kDepth16 = 0x81A5;

using PFNTexImage3D = void(APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using PFNTexImage2DMultisample = void(APIENTRY*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean);
using PFNGenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
using PFNDeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using PFNBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using PFNFramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFNFramebufferTextureLayer = void(APIENTRY*)(GLenum, GLenum, GLuint, GLint, GLint);
using PFNCheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
using PFNBindBuffer = void(APIENTRY*)(GLenum, GLuint);
using PFNCopyImageSubData = void(APIENTRY*)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint,
    GLint, GLsizei, GLsizei, GLsizei);

struct GlFns {
	PFNTexImage3D TexImage3D = nullptr;
	PFNTexImage2DMultisample TexImage2DMultisample = nullptr;
	PFNGenFramebuffers GenFramebuffers = nullptr;
	PFNDeleteFramebuffers DeleteFramebuffers = nullptr;
	PFNBindFramebuffer BindFramebuffer = nullptr;
	PFNFramebufferTexture2D FramebufferTexture2D = nullptr;
	PFNFramebufferTextureLayer FramebufferTextureLayer = nullptr;
	PFNCheckFramebufferStatus CheckFramebufferStatus = nullptr;
	PFNBindBuffer BindBuffer = nullptr;
	PFNCopyImageSubData CopyImageSubData = nullptr; // GL 4.3
	bool Load()
	{
		TexImage3D = (PFNTexImage3D)wglGetProcAddress("glTexImage3D");
		TexImage2DMultisample = (PFNTexImage2DMultisample)wglGetProcAddress("glTexImage2DMultisample");
		GenFramebuffers = (PFNGenFramebuffers)wglGetProcAddress("glGenFramebuffers");
		DeleteFramebuffers = (PFNDeleteFramebuffers)wglGetProcAddress("glDeleteFramebuffers");
		BindFramebuffer = (PFNBindFramebuffer)wglGetProcAddress("glBindFramebuffer");
		FramebufferTexture2D = (PFNFramebufferTexture2D)wglGetProcAddress("glFramebufferTexture2D");
		FramebufferTextureLayer = (PFNFramebufferTextureLayer)wglGetProcAddress("glFramebufferTextureLayer");
		CheckFramebufferStatus = (PFNCheckFramebufferStatus)wglGetProcAddress("glCheckFramebufferStatus");
		BindBuffer = (PFNBindBuffer)wglGetProcAddress("glBindBuffer");
		CopyImageSubData = (PFNCopyImageSubData)wglGetProcAddress("glCopyImageSubData");
		return GenFramebuffers && DeleteFramebuffers && BindFramebuffer && FramebufferTexture2D && CheckFramebufferStatus && BindBuffer;
	}
};
GlFns g_gl;

// The session's context current on this thread while it lives (and the one before it back afterwards)
class Current {
public:
	explicit Current(RtSession* s)
	{
		prevDC = wglGetCurrentDC();
		prevRC = wglGetCurrentContext();
		if (prevRC == s->glRC) {
			ok = true;
			return;
		}
		ok = wglMakeCurrent(s->glDC, s->glRC) != FALSE;
		switched = ok;
	}
	~Current()
	{
		if (switched)
			wglMakeCurrent(prevDC, prevRC);
	}
	bool ok = false;

private:
	HDC prevDC = nullptr;
	HGLRC prevRC = nullptr;
	bool switched = false;
};

XrResult XRAPI_CALL GetOpenGLGraphicsRequirements(XrInstance, XrSystemId, XrGraphicsRequirementsOpenGLKHR* reqs)
{
	XrResult injected;
	if (ScriptFails("xrGetOpenGLGraphicsRequirementsKHR", &injected))
		return injected;
	{
		std::lock_guard<std::recursive_mutex> g(St().lock);
		St().requirementsQueried.insert(RtApi::OpenGL);
	}
	reqs->minApiVersionSupported = XR_MAKE_VERSION(3, 0, 0);
	reqs->maxApiVersionSupported = XR_MAKE_VERSION(4, 6, 0);
	return XR_SUCCESS;
}

XrResult Bind(const XrBaseInStructure* binding, RtSession* s)
{
	auto* b = (const XrGraphicsBindingOpenGLWin32KHR*)binding;
	if (!b->hDC || !b->hGLRC)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	s->glDC = b->hDC;
	s->glRC = b->hGLRC;
	Current current(s);
	if (current.ok && g_gl.Load())
		return XR_SUCCESS;
	s->glDC = nullptr;
	s->glRC = nullptr;
	return XR_ERROR_GRAPHICS_DEVICE_INVALID;
}

void Unbind(RtSession* s)
{
	s->glDC = nullptr;
	s->glRC = nullptr;
}

const std::vector<int64_t>& Formats()
{
	static const std::vector<int64_t> formats = { kSRGB8Alpha8, kRGBA8, kRGBA16F, kRGB10A2, kDepth32F, kDepth24, kDepth16 };
	return formats;
}

const std::vector<int64_t>& DefaultFormats()
{
	static const std::vector<int64_t> formats = { kSRGB8Alpha8, kRGBA8 };
	return formats;
}

bool IsDepth(int64_t format)
{
	return format == kDepth32F || format == kDepth24 || format == kDepth16;
}

// The layout Read returns the stored texels in (depth isn't read)
PixelLayout Layout(int64_t format)
{
	if (IsDepth(format))
		return PixelLayout::Depth;
	if (format == kSRGB8Alpha8 || format == kRGBA8)
		return PixelLayout::RGBA8;
	if (format == kRGBA16F)
		return PixelLayout::RGBA16F;
	if (format == kRGB10A2)
		return PixelLayout::RGB10A2;
	return PixelLayout::Unknown;
}

GLenum TargetOf(const XrSwapchainCreateInfo& info)
{
	if (info.sampleCount > 1)
		return kTexture2DMultisample;
	if (info.faceCount == 6)
		return kTextureCubeMap;
	if (info.arraySize > 1)
		return kTexture2DArray;
	return GL_TEXTURE_2D;
}

GLenum BindingOf(GLenum target)
{
	switch (target) {
	case kTexture2DMultisample:
		return kTextureBinding2DMultisample;
	case kTextureCubeMap:
		return kTextureBindingCubeMap;
	case kTexture2DArray:
		return kTextureBinding2DArray;
	default:
		return GL_TEXTURE_BINDING_2D;
	}
}

void Destroy(RtSession* s, RtSwapchain* sc)
{
	if (s && s->glRC && !sc->images.empty()) {
		Current current(s);
		if (current.ok) {
			for (uint64_t name : sc->images) {
				GLuint n = (GLuint)name;
				glDeleteTextures(1, &n);
			}
		}
	}
	sc->images.clear();
}

XrResult Create(RtSession* s, const XrSwapchainCreateInfo* info, RtSwapchain* sc)
{
	if (!s->glRC)
		return XR_ERROR_VALIDATION_FAILURE;
	if (std::find(Formats().begin(), Formats().end(), info->format) == Formats().end())
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	Current current(s);
	if (!current.ok)
		return XR_ERROR_RUNTIME_FAILURE;
	GLenum target = TargetOf(*info);
	if ((target == kTexture2DArray && !g_gl.TexImage3D) || (target == kTexture2DMultisample && !g_gl.TexImage2DMultisample))
		return XR_ERROR_FEATURE_UNSUPPORTED;
	bool depth = IsDepth(info->format);
	GLenum external = depth ? GL_DEPTH_COMPONENT : GL_RGBA, type = depth ? GL_FLOAT : GL_UNSIGNED_BYTE;
	GLint previous = 0;
	glGetIntegerv(BindingOf(target), &previous);
	// Earlier errors cleared (at most 32: the context reports each one once)
	for (int i = 0; i < 32 && glGetError() != GL_NO_ERROR; i++) {
	}
	sc->info = *info;
	uint32_t mips = info->mipCount ? info->mipCount : 1;
	for (int i = 0; i < 3; i++) {
		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(target, tex);
		if (target == kTexture2DMultisample) {
			g_gl.TexImage2DMultisample(target, (GLsizei)info->sampleCount, (GLenum)info->format, (GLsizei)info->width, (GLsizei)info->height, GL_TRUE);
		} else {
			for (uint32_t m = 0; m < mips; m++) {
				GLsizei w = (GLsizei)std::max(1u, info->width >> m), h = (GLsizei)std::max(1u, info->height >> m);
				if (target == kTexture2DArray)
					g_gl.TexImage3D(target, (GLint)m, (GLint)info->format, w, h, (GLsizei)info->arraySize, 0, external, type, nullptr);
				else if (target == kTextureCubeMap)
					for (GLenum f = 0; f < 6; f++)
						glTexImage2D(kCubeMapPositiveX + f, (GLint)m, (GLint)info->format, w, h, 0, external, type, nullptr);
				else
					glTexImage2D(target, (GLint)m, (GLint)info->format, w, h, 0, external, type, nullptr);
			}
			glTexParameteri(target, 0x813D /* GL_TEXTURE_MAX_LEVEL */, (GLint)mips - 1);
		}
		sc->images.push_back(tex);
	}
	glBindTexture(target, (GLuint)previous);
	if (glGetError() != GL_NO_ERROR) {
		Destroy(s, sc);
		return XR_ERROR_RUNTIME_FAILURE;
	}
	return XR_SUCCESS;
}

void Enumerate(const RtSwapchain* sc, XrSwapchainImageBaseHeader* images)
{
	auto* out = (XrSwapchainImageOpenGLKHR*)images;
	for (size_t i = 0; i < sc->images.size(); i++)
		out[i].image = (uint32_t)sc->images[i];
}

// A read framebuffer on one layer (face) of a texture, the one before it back when it goes
class ReadFramebuffer {
public:
	ReadFramebuffer(GLenum target, GLuint tex, uint32_t layer)
	{
		glGetIntegerv(kReadFramebufferBinding, &previous);
		g_gl.GenFramebuffers(1, &fbo);
		g_gl.BindFramebuffer(kReadFramebuffer, fbo);
		if (target == kTexture2DArray)
			g_gl.FramebufferTextureLayer(kReadFramebuffer, kColorAttachment0, tex, 0, (GLint)layer);
		else if (target == kTextureCubeMap)
			g_gl.FramebufferTexture2D(kReadFramebuffer, kColorAttachment0, kCubeMapPositiveX + layer % 6, tex, 0);
		else
			g_gl.FramebufferTexture2D(kReadFramebuffer, kColorAttachment0, GL_TEXTURE_2D, tex, 0);
		ok = g_gl.CheckFramebufferStatus(kReadFramebuffer) == kFramebufferComplete;
	}
	~ReadFramebuffer()
	{
		g_gl.BindFramebuffer(kReadFramebuffer, (GLuint)previous);
		g_gl.DeleteFramebuffers(1, &fbo);
	}
	bool ok = false;

private:
	GLint previous = 0;
	GLuint fbo = 0;
};

// The stored texels, unconverted: 8-bit formats are copied raw (glCopyImageSubData, both in the 32-bit class) into an
// RGBA8 scratch texture and read from that, so an sRGB texture gives its encoded bytes whatever the driver would make
// of reading it; RGBA16F and RGB10A2 are read in their own type
bool Read(RtSession* s, RtSwapchain* sc, uint32_t index, uint32_t arrayIndex, const std::vector<XrOffset2Di>& points,
    uint32_t bytes, std::vector<uint8_t>& out, std::string* why)
{
	*why = "the OpenGL readback failed";
	if (!s->glRC || index >= sc->images.size() || points.empty())
		return false;
	GLenum target = TargetOf(sc->info);
	if (target == kTexture2DMultisample)
		return false; // a multisampled texture isn't read back
	if (target == kTexture2DArray && !g_gl.FramebufferTextureLayer)
		return false;
	bool eightBit = sc->info.format == kSRGB8Alpha8 || sc->info.format == kRGBA8;
	if (eightBit && !g_gl.CopyImageSubData) {
		*why = "OpenGL 4.3 (glCopyImageSubData) is needed to read the stored texels";
		return false;
	}
	Current current(s);
	if (!current.ok) {
		*why = "the session's OpenGL context can't be made current here";
		return false;
	}
	GLint prevPack = 0, prevAlign = 0, prevTex = 0;
	glGetIntegerv(kPixelPackBufferBinding, &prevPack);
	glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlign);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
	// Earlier errors cleared (at most 32: the context reports each one once)
	for (int i = 0; i < 32 && glGetError() != GL_NO_ERROR; i++) {
	}
	g_gl.BindBuffer(kPixelPackBuffer, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	auto tex = (GLuint)sc->images[index];
	auto clampX = [&](int32_t x) { return std::min<int32_t>(std::max<int32_t>(x, 0), (int32_t)sc->info.width - 1); };
	auto clampY = [&](int32_t y) { return std::min<int32_t>(std::max<int32_t>(y, 0), (int32_t)sc->info.height - 1); };
	out.assign(points.size() * bytes, 0);
	bool ok = false;
	if (eightBit) {
		GLuint scratch = 0;
		glGenTextures(1, &scratch);
		glBindTexture(GL_TEXTURE_2D, scratch);
		glTexImage2D(GL_TEXTURE_2D, 0, (GLint)kRGBA8, (GLsizei)points.size(), 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, 0x813D /* GL_TEXTURE_MAX_LEVEL */, 0);
		GLint srcZ = target == GL_TEXTURE_2D ? 0 : (GLint)arrayIndex;
		for (size_t i = 0; i < points.size(); i++)
			g_gl.CopyImageSubData(tex, target, 0, clampX(points[i].x), clampY(points[i].y), srcZ, scratch, GL_TEXTURE_2D, 0, (GLint)i, 0, 0, 1, 1, 1);
		{
			ReadFramebuffer fb(GL_TEXTURE_2D, scratch, 0);
			if (fb.ok)
				glReadPixels(0, 0, (GLsizei)points.size(), 1, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
			ok = fb.ok;
		}
		glDeleteTextures(1, &scratch);
	} else {
		GLenum type = sc->info.format == kRGBA16F ? kHalfFloat : kUnsignedInt2101010Rev;
		ReadFramebuffer fb(target, tex, arrayIndex);
		ok = fb.ok;
		for (size_t i = 0; ok && i < points.size(); i++)
			glReadPixels(clampX(points[i].x), clampY(points[i].y), 1, 1, GL_RGBA, type, &out[i * bytes]);
	}
	ok = ok && glGetError() == GL_NO_ERROR;
	glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
	glPixelStorei(GL_PACK_ALIGNMENT, prevAlign);
	g_gl.BindBuffer(kPixelPackBuffer, (GLuint)prevPack);
	return ok;
}

} // namespace

const RtGfx& RtOpenGLGfx()
{
	static const RtGfx gfx = {
		RtApi::OpenGL, "opengl", "XR_KHR_opengl_enable", nullptr, XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR, XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR,
		Bind, Unbind, Formats, DefaultFormats, Create, Destroy, Enumerate, Layout, true, Read,
	};
	return gfx;
}

PFN_xrVoidFunction RtOpenGLLookup(const char* name)
{
	if (!std::strcmp(name, "xrGetOpenGLGraphicsRequirementsKHR"))
		return (PFN_xrVoidFunction)GetOpenGLGraphicsRequirements;
	return nullptr;
}
