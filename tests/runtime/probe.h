#pragma once
// Pixel readback of the stand-in runtime's swapchain images, for the dump's layer records: the values at a rect's
// four corners (2 pixels in) and at its centre, as RGBA8.

#include "runtime_internal.h"

#include <cstdint>
#include <string>

// RGBA8 values, 0xRRGGBBAA, of the rect as shown: tl is its top-left corner whatever the API's row order
struct Samples {
	uint32_t tl = 0, tr = 0, bl = 0, br = 0, c = 0;
};

struct SwapchainImage {
	RtSession* session = nullptr;
	RtSwapchain* swapchain = nullptr;
	uint32_t index = 0;
};

// The layout of a swapchain format's texels, whatever the API's enum for it
enum class PixelLayout { Unknown, RGBA8, BGRA8, RGBA16F, RGB10A2, Depth };
PixelLayout LayoutOfDxgi(int64_t format);

// Reads the samples of rect in layer arrayIndex of the image, converted to RGBA8. False when it can't be read (no
// such image, a depth or unknown format, an empty rect).
bool ProbeSamples(SwapchainImage& image, XrRect2Di rect, uint32_t arrayIndex, Samples& out);

// {"tl": "ff0000ff", "tr": ..., "bl": ..., "br": ..., "c": ...}
Json::Value SamplesJson(const Samples& s);
