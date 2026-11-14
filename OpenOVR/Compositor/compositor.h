#pragma once

#include "generated/interfaces/vrtypes.h"
#ifdef WIN32
// Windows template libraries
#include <atlbase.h>
#include <wrl/client.h>
#endif

#include "../Misc/xr_ext.h"
#include "../Misc/xrutil.h"

#include <memory>
#include <optional>
#include <vector>

#ifdef WIN32
using Microsoft::WRL::ComPtr;
#endif

typedef unsigned int GLuint;

class Compositor {
public:
	virtual ~Compositor();

	void Invoke(const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds, XrSwapchainSubImage& subImage, std::optional<XruEye> eye = std::nullopt, vr::EVRSubmitFlags submitFlags = vr::Submit_Default);
	bool CalculateViewport(const vr::VRTextureBounds_t* bounds, int32_t width, int32_t height, bool supportsInvert, XrRect2Di& viewport);

	virtual void InvokeCubemap(const vr::Texture_t* textures) = 0;
	virtual bool SupportsCubemap() { return false; }

	virtual XrSwapchain GetSwapChain() { return chain; };

	/**
	 * What the last Invoke ran into: XR_SUCCESS, or the OpenXR swapchain call that failed (that eye's image is then
	 * left out of the frame, and Submit answers an error). The next Invoke tries again.
	 */
	XrResult LastResult() const { return lastResult; }

	virtual XrExtent2Di GetSrcSize() { return { static_cast<int32_t>(createInfo.width), static_cast<int32_t>(createInfo.height) }; }
	/**
	 * Loads and unloads some context required for submitting textures to LibOVR. LoadSubmitContext is
	 *  called before calling either Invoke or ovr_CommitTextureSwapChain, and ResetSubmitContext after
	 *  calling both of them.
	 */
	virtual void LoadSubmitContext() {};
	virtual void ResetSubmitContext() {};

protected:
	virtual void CopyToSwapchain(const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds, std::optional<XruEye> eye, vr::EVRSubmitFlags submitFlags) = 0;

	// For the compositors' OpenXR swapchain calls: true (with lastResult set, and the call logged once) if it failed
	bool SwapchainCallFailed(XrResult res, const char* what);
	XrResult lastResult = XR_SUCCESS;

	/**
	 * Acquires an image of chain and waits for it: true with its index, else false with lastResult set. After a
	 * timed-out wait the same image is waited on at the next call, so an image is never left acquired. An index
	 * beyond imageCount is released unwritten. The default timeout is 500 ms.
	 */
	bool AcquireAndWait(uint32_t imageCount, uint32_t& index, int64_t timeout = 500000000);
	// For a new swapchain (the old one's pending image went with it)
	void ForgetAcquiredImage() { waitPending = false; }

private:
	bool waitPending = false;
	uint32_t pendingIndex = 0;
	XrSwapchain pendingChain = XR_NULL_HANDLE;
	// Invoke's back-off after failures
	int failuresInARow = 0;
	int retryDelay = 0;
	XrResult lastFailure = XR_SUCCESS;

protected:

	XrSwapchain chain = XR_NULL_HANDLE;

	// The request used to create the current swapchain. This can be used to check if the swapchain needs recreating.
	XrSwapchainCreateInfo createInfo{};

	// The format specified by the game when creating the swapchain. This is used for verifying the format hasn't changed, since
	// we do fiddle with it a bit to get the SRGB stuff done correctly.
	int64_t createInfoFormat;
};
