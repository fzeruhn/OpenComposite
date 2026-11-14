#include "probe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

PixelLayout LayoutOfDxgi(int64_t format)
{
	switch (format) {
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return PixelLayout::RGBA8;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return PixelLayout::BGRA8;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return PixelLayout::RGBA16F;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
		return PixelLayout::RGB10A2;
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		return PixelLayout::Depth;
	default:
		return PixelLayout::Unknown;
	}
}

namespace {

uint32_t BytesOf(PixelLayout layout)
{
	switch (layout) {
	case PixelLayout::RGBA8:
	case PixelLayout::BGRA8:
	case PixelLayout::RGB10A2:
		return 4;
	case PixelLayout::RGBA16F:
		return 8;
	default:
		return 0;
	}
}

float HalfToFloat(uint16_t h)
{
	uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 31, mant = h & 1023;
	float v;
	if (exp == 0)
		v = std::ldexp((float)mant, -24);
	else if (exp == 31)
		v = mant ? NAN : INFINITY;
	else
		v = std::ldexp((float)(mant | 1024), (int)exp - 25);
	return sign ? -v : v;
}

uint32_t Unorm8(float v)
{
	if (!(v > 0))
		return 0;
	if (v >= 1)
		return 255;
	return (uint32_t)std::lround(v * 255.0f);
}

uint32_t ToRgba8(PixelLayout layout, const uint8_t* t)
{
	uint32_t r = 0, g = 0, b = 0, a = 0;
	switch (layout) {
	case PixelLayout::RGBA8:
		r = t[0], g = t[1], b = t[2], a = t[3];
		break;
	case PixelLayout::BGRA8:
		r = t[2], g = t[1], b = t[0], a = t[3];
		break;
	case PixelLayout::RGBA16F: {
		uint16_t h[4];
		std::memcpy(h, t, 8);
		r = Unorm8(HalfToFloat(h[0])), g = Unorm8(HalfToFloat(h[1])), b = Unorm8(HalfToFloat(h[2])), a = Unorm8(HalfToFloat(h[3]));
		break;
	}
	case PixelLayout::RGB10A2: {
		uint32_t v;
		std::memcpy(&v, t, 4);
		r = (uint32_t)std::lround((v & 1023) * 255.0 / 1023), g = (uint32_t)std::lround(((v >> 10) & 1023) * 255.0 / 1023);
		b = (uint32_t)std::lround(((v >> 20) & 1023) * 255.0 / 1023), a = ((v >> 30) & 3) * 85;
		break;
	}
	default:
		break;
	}
	return (r << 24) | (g << 16) | (b << 8) | a;
}

std::string Hex(uint32_t v)
{
	char buf[9];
	std::snprintf(buf, sizeof(buf), "%08x", v);
	return buf;
}

} // namespace

bool ProbeSamples(SwapchainImage& image, XrRect2Di rect, uint32_t arrayIndex, Samples& out)
{
	RtSwapchain* sc = image.swapchain;
	if (!sc || !image.session || image.index >= sc->textures.size())
		return false;
	PixelLayout layout = LayoutOfDxgi(sc->info.format);
	uint32_t bytes = BytesOf(layout);
	if (!bytes)
		return false;
	// The part of the rect inside the image
	int32_t w = (int32_t)sc->info.width, h = (int32_t)sc->info.height;
	int32_t x0 = std::max(rect.offset.x, 0), y0 = std::max(rect.offset.y, 0);
	int32_t x1 = std::min(rect.offset.x + rect.extent.width, w), y1 = std::min(rect.offset.y + rect.extent.height, h);
	if (x1 <= x0 || y1 <= y0)
		return false;
	uint32_t layers = (sc->info.arraySize ? sc->info.arraySize : 1) * (sc->info.faceCount ? sc->info.faceCount : 1);
	if (arrayIndex >= layers)
		return false;
	// 2 pixels in from each edge, or as far as a small rect allows
	int32_t ix = std::min(2, (x1 - x0 - 1) / 2), iy = std::min(2, (y1 - y0 - 1) / 2);
	int32_t left = x0 + ix, right = x1 - 1 - ix, top = y0 + iy, bottom = y1 - 1 - iy;
	std::vector<XrOffset2Di> points = {
		{ left, top }, { right, top }, { left, bottom }, { right, bottom }, { (x0 + x1) / 2, (y0 + y1) / 2 },
	};
	std::vector<uint8_t> texels;
	if (!RtD3D11ReadTexels(image.session, sc, image.index, arrayIndex, points, bytes, texels) || texels.size() < 5 * bytes)
		return false;
	out.tl = ToRgba8(layout, &texels[0 * bytes]);
	out.tr = ToRgba8(layout, &texels[1 * bytes]);
	out.bl = ToRgba8(layout, &texels[2 * bytes]);
	out.br = ToRgba8(layout, &texels[3 * bytes]);
	out.c = ToRgba8(layout, &texels[4 * bytes]);
	return true;
}

Json::Value SamplesJson(const Samples& s)
{
	Json::Value v(Json::objectValue);
	v["tl"] = Hex(s.tl);
	v["tr"] = Hex(s.tr);
	v["bl"] = Hex(s.bl);
	v["br"] = Hex(s.br);
	v["c"] = Hex(s.c);
	return v;
}
