#pragma once
// The game's side of the frame cases (gfx_cases.cpp): a device of one graphics API, and textures of a test pattern to
// submit through it. The device is the hardware adapter's (the first Vulkan physical device), or WARP for the D3D
// kinds when there is none.
//
// The pattern: quadrants top-left red, top-right green, bottom-left blue, bottom-right white, as shown in the
// headset (an OpenGL texture's rows run bottom-up). Eye 1 has them rotated once clockwise: top-left blue, top-right
// red, bottom-right green, bottom-left white.

#include "callers_support.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

enum class GfxKind { D3D11, D3D12, Vulkan, OpenGL, D3D10 };

// A texture to submit: tex.handle points into vk or d3d12 for those APIs (Texture() sets it, so a copied GfxTexture
// still works); keep is the API's own object (ID3D11Texture2D*, ID3D12Resource*, VkImage, GL texture name,
// ID3D10Texture2D*), owned by the GfxApi that made it. A texture that couldn't be made has a null tex.handle.
struct GfxTexture {
	vr::Texture_t tex{};
	vr::VRVulkanTextureData_t vk{};
	vr::D3D12TextureData_t d3d12{};
	void* keep = nullptr;

	vr::Texture_t* Texture()
	{
		if (tex.eType == vr::TextureType_Vulkan)
			tex.handle = keep ? &vk : nullptr;
		else if (tex.eType == vr::TextureType_DirectX12)
			tex.handle = keep ? &d3d12 : nullptr;
		return &tex;
	}
	// Vulkan's VkImage is 64-bit on 32-bit Windows too, more than keep holds there
	bool Made() const { return tex.eType == vr::TextureType_Vulkan ? vk.m_nImage != 0 : keep != nullptr; }
};

class GfxApi {
public:
	// nullptr, with why in *whyNot, when no device of the kind can be made here
	static std::unique_ptr<GfxApi> Create(GfxKind kind, std::string* whyNot); // WARP for the D3D kinds when no adapter
	// The test pattern (above), w x h, in nativeFormat (the API's own enum value), with samples samples and arraySize
	// layers (each the same)
	virtual GfxTexture Pattern(uint32_t w, uint32_t h, int64_t nativeFormat, int eye, uint32_t samples = 1, uint32_t arraySize = 1) = 0;
	// A depth texture (32-bit float) filled with value
	virtual GfxTexture Depth(uint32_t w, uint32_t h, float value) = 0;
	// Waits until everything the device was asked to do is done
	virtual void Flush() = 0;
	virtual ~GfxApi() = default;
};

namespace gfx {

// The pattern's colour at (x, y) from the top-left as shown, 0xRRGGBBAA
inline uint32_t PatternColour(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int eye)
{
	static const uint32_t quadrants[4] = { 0xff0000ff, 0x00ff00ff, 0x0000ffff, 0xffffffff }; // tl, tr, bl, br
	int q = (y >= h / 2 ? 2 : 0) + (x >= w / 2 ? 1 : 0);
	if (eye == 1) {
		// Once clockwise: the top-left shows what was bottom-left, and so on round
		static const int from[4] = { 2, 0, 3, 1 };
		q = from[q];
	}
	return quadrants[q];
}

// How a format's texels are laid out in memory
enum class Texels { Unknown, RGBA8, BGRA8, RGBA16F, RGB10A2 };

inline uint16_t FloatToHalf(float f)
{
	// Only 0 and 1 are needed for the pattern
	return f >= 1.0f ? 0x3C00 : 0;
}

inline uint32_t BytesOf(Texels t)
{
	return t == Texels::RGBA16F ? 8 : t == Texels::Unknown ? 0 : 4;
}

inline void WriteTexel(Texels t, uint32_t rgba, uint8_t* out)
{
	uint8_t r = (uint8_t)(rgba >> 24), g = (uint8_t)(rgba >> 16), b = (uint8_t)(rgba >> 8), a = (uint8_t)rgba;
	switch (t) {
	case Texels::RGBA8:
		out[0] = r, out[1] = g, out[2] = b, out[3] = a;
		break;
	case Texels::BGRA8:
		out[0] = b, out[1] = g, out[2] = r, out[3] = a;
		break;
	case Texels::RGBA16F: {
		uint16_t h[4] = { FloatToHalf(r / 255.0f), FloatToHalf(g / 255.0f), FloatToHalf(b / 255.0f), FloatToHalf(a / 255.0f) };
		std::memcpy(out, h, 8);
		break;
	}
	case Texels::RGB10A2: {
		uint32_t v = (uint32_t)(r * 1023 / 255) | ((uint32_t)(g * 1023 / 255) << 10) | ((uint32_t)(b * 1023 / 255) << 20) | ((uint32_t)(a * 3 / 255) << 30);
		std::memcpy(out, &v, 4);
		break;
	}
	default:
		break;
	}
}

// The pattern's texels, rowPitch bytes a row; bottomUp puts the bottom row (as shown) first
inline std::vector<uint8_t> PatternTexels(Texels t, uint32_t w, uint32_t h, int eye, size_t rowPitch, bool bottomUp)
{
	std::vector<uint8_t> data(rowPitch * h);
	uint32_t bytes = BytesOf(t);
	for (uint32_t row = 0; row < h; row++) {
		uint32_t y = bottomUp ? h - 1 - row : row;
		for (uint32_t x = 0; x < w; x++)
			WriteTexel(t, PatternColour(x, y, w, h, eye), &data[row * rowPitch + (size_t)x * bytes]);
	}
	return data;
}

} // namespace gfx

#ifdef _WIN32
// gfx/*Gfx.cpp
std::unique_ptr<GfxApi> MakeD3D11Gfx(std::string* whyNot);
std::unique_ptr<GfxApi> MakeD3D12Gfx(std::string* whyNot);
std::unique_ptr<GfxApi> MakeVkGfx(std::string* whyNot);
std::unique_ptr<GfxApi> MakeGLGfx(std::string* whyNot);
std::unique_ptr<GfxApi> MakeD3D10Gfx(std::string* whyNot);
gfx::Texels TexelsOfDxgi(int64_t format);
#endif
