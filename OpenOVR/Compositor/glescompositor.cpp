#include "stdafx.h"

#ifdef SUPPORT_GLES

#include "glescompositor.h"

#include <GLES3/gl32.h>

#include <algorithm>
#include <string>

GLESCompositor::GLESCompositor() = default;

void GLESCompositor::ReadSwapchainImages()
{
	// Enumerate all the swapchain images
	uint32_t imageCount = 0;
	images.clear();
	bool listed = !SwapchainCallFailed(xrEnumerateSwapchainImages(chain, 0, &imageCount, nullptr), "xrEnumerateSwapchainImages");
	auto handles = std::vector<XrSwapchainImageOpenGLESKHR>(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
	listed = listed && !SwapchainCallFailed(xrEnumerateSwapchainImages(chain, imageCount, &imageCount, (XrSwapchainImageBaseHeader*)handles.data()), "xrEnumerateSwapchainImages");
	if (!listed || imageCount == 0) {
		// A swapchain without images can't be used: it goes, and is made again at a later Submit
		lastResult = XR_FAILED(lastResult) ? lastResult : XR_ERROR_RUNTIME_FAILURE;
		xrDestroySwapchain(chain);
		chain = XR_NULL_HANDLE;
		createInfo = {};
		return;
	}

	for (const XrSwapchainImageOpenGLESKHR& img : handles) {
		images.push_back(img.image);
	}
}

#endif
