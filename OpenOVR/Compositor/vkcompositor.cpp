
#if defined(SUPPORT_VK)

#include "vkcompositor.h"
#include "generated/interfaces/vrtypes.h"
#include "stdafx.h"

#include <vulkan/vulkan.h>

#include "../DrvOpenXR/XrBackend.h"
// Required for the close(2) call for the texture shared memory on Linux
#ifndef _WIN32
#include <unistd.h>
#endif

static enum VkFormat handle_colorspace_auto(enum VkFormat ovrFormat)
{
	switch (ovrFormat) {
	// convert to SRGB
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_R8G8B8A8_SRGB:
		return VK_FORMAT_R8G8B8A8_SRGB;
		break;
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		return VK_FORMAT_B8G8R8A8_SRGB;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
	case VK_FORMAT_R32G32B32_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_A2R10G10B10_UINT_PACK32:
		// Todo what games actually use these? and is color space always linear for these?
		return ovrFormat;
	default:
		// VK_FORMAT_UNDEFINED: Submit answers TextureUsesUnsupportedFormat
		OOVR_LOGF("Unsupported texture format used: %d", ovrFormat);
		return VK_FORMAT_UNDEFINED;
	}
}

static enum VkFormat handle_colorspace_gamma(enum VkFormat ovrFormat)
{
	switch (ovrFormat) {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		return VK_FORMAT_R8G8B8A8_SRGB;
		break;
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		return VK_FORMAT_B8G8R8A8_SRGB;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
	case VK_FORMAT_R32G32B32_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_A2R10G10B10_UINT_PACK32:
		// Todo what games actually use these? and is color space always linear for these?
		return ovrFormat;
	default:
		// VK_FORMAT_UNDEFINED: Submit answers TextureUsesUnsupportedFormat
		OOVR_LOGF("Unsupported texture format used: %d", ovrFormat);
		return VK_FORMAT_UNDEFINED;
	}
}

static enum VkFormat handle_colorspace_linear(enum VkFormat ovrFormat)
{
	switch (ovrFormat) {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		return VK_FORMAT_R8G8B8A8_UNORM;
		break;
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
	case VK_FORMAT_R32G32B32_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_A2R10G10B10_UINT_PACK32:
		// Todo what games actually use these? and is color space always linear for these?
		return ovrFormat;
	default:
		// VK_FORMAT_UNDEFINED: Submit answers TextureUsesUnsupportedFormat
		OOVR_LOGF("Unsupported texture format used: %d", ovrFormat);
		return VK_FORMAT_UNDEFINED;
	}
}

VkCompositor::VkCompositor(const vr::Texture_t* initialTexture)
{
	auto* tex = (vr::VRVulkanTextureData_t*)initialTexture->handle;

	appDevice = tex->m_pDevice;
	appQueue = tex->m_pQueue;

	VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	// Its command buffers go to the app's queue: use the family the backend found that queue in, else the app's word
	poolInfo.queueFamilyIndex = tex->m_nQueueFamilyIndex;
	auto* binding = (const XrGraphicsBindingVulkanKHR*)((XrBackend*)BackendManager::Instance().GetBackendInstance())->GetCurrentGraphicsBinding();
	if (binding && binding->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR && binding->device == tex->m_pDevice) {
		VkQueue queue = VK_NULL_HANDLE;
		vkGetDeviceQueue(binding->device, binding->queueFamilyIndex, binding->queueIndex, &queue);
		if (queue == tex->m_pQueue)
			poolInfo.queueFamilyIndex = binding->queueFamilyIndex;
	}
	// Without one nothing can be copied: every Submit then answers RequestFailed
	if (vkCreateCommandPool(tex->m_pDevice, &poolInfo, nullptr, &appCommandPool) != VK_SUCCESS) {
		OOVR_LOG("vkCreateCommandPool failed: Vulkan textures can't be copied");
		appCommandPool = VK_NULL_HANDLE;
	}
}

VkCompositor::~VkCompositor()
{
	// destroying command pool also frees command buffers
	if (appCommandPool)
		vkDestroyCommandPool(appDevice, appCommandPool, nullptr);
}

void VkCompositor::CopyToSwapchain(const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds, std::optional<XruEye>, vr::EVRSubmitFlags submitFlags)
{
	const vr::VRVulkanTextureData_t* tex = (vr::VRVulkanTextureData_t*)texture->handle;

	// XR_ERROR_VALIDATION_FAILURE: Submit answers InvalidTexture
	if (!tex || !tex->m_pDevice || !tex->m_pQueue) {
		OOVR_LOG_ONCE("Cannot use NULL Vulkan image data (VRVulkanTextureData_t)");
		lastResult = XR_ERROR_VALIDATION_FAILURE;
		return;
	}

	// OpenXR only guarantees that the queue we initially gave it will have ownership of our swapchain image
	// We could safeguard against any insane applications with a queue that we manage and copy to it,
	// but it's simpler (and likely more performant) to just assume our app is sane.
	if (appQueue != tex->m_pQueue) {
		OOVR_LOG_ONCE("A Vulkan texture on another queue than the first one: not used");
		lastResult = XR_ERROR_VALIDATION_FAILURE;
		return;
	}
	if (!appCommandPool) {
		lastResult = XR_ERROR_RUNTIME_FAILURE;
		return;
	}

	bool usable = chain != XR_NULL_HANDLE && CheckChainCompatible(*tex, createInfo, texture->eColorSpace);

	if (!usable) {
		OOVR_LOG("Generating new swap chain");

		// First, delete the old chain if necessary
		if (chain)
			xrDestroySwapchain(chain);

		// Free old command buffers if necessary
		if (!appCommandBuffers.empty()) {
			vkFreeCommandBuffers(appDevice, appCommandPool, appCommandBuffers.size(), appCommandBuffers.data());
			appCommandBuffers.clear();
		}

		// Make eye render buffer
		createInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		createInfo.createFlags = 0;
		createInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		createInfo.faceCount = 1;
		createInfo.width = tex->m_nWidth;
		createInfo.height = tex->m_nHeight;
		createInfo.mipCount = 1;
		createInfo.sampleCount = tex->m_nSampleCount;
		createInfo.arraySize = 1;

		switch (texture->eColorSpace) {
		case vr::ColorSpace_Auto: {
			createInfo.format = handle_colorspace_auto((VkFormat)tex->m_nFormat);
			break;
		}
		case vr::ColorSpace_Gamma: {
			createInfo.format = handle_colorspace_gamma((VkFormat)tex->m_nFormat);
			break;
		}
		case vr::ColorSpace_Linear: {
			createInfo.format = handle_colorspace_linear((VkFormat)tex->m_nFormat);
			break;
		}
		default:
			createInfo.format = VK_FORMAT_UNDEFINED;
			break;
		}
		if (createInfo.format == VK_FORMAT_UNDEFINED) {
			chain = XR_NULL_HANDLE;
			lastResult = XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
			return;
		}

		if (SwapchainCallFailed(xrCreateSwapchain(xr_session.get(), &createInfo, &chain), "xrCreateSwapchain")) {
			chain = XR_NULL_HANDLE; // tried again at the next Submit
			return;
		}

		uint32_t chainLength = 0;
		bool listed = !SwapchainCallFailed(xrEnumerateSwapchainImages(chain, 0, &chainLength, nullptr), "xrEnumerateSwapchainImages");
		if (listed) {
			swapchainImages.resize(chainLength);
			for (XrSwapchainImageVulkanKHR& swapchainImage : swapchainImages)
				swapchainImage.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
			listed = !SwapchainCallFailed(xrEnumerateSwapchainImages(chain, swapchainImages.size(), &chainLength, (XrSwapchainImageBaseHeader*)swapchainImages.data()), "xrEnumerateSwapchainImages");
		}
		if (!listed) {
			swapchainImages.clear();
			xrDestroySwapchain(chain);
			chain = XR_NULL_HANDLE;
			return;
		}

		appCommandBuffers.resize(chainLength);
		VkCommandBufferAllocateInfo bufInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		bufInfo.commandPool = appCommandPool;
		bufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		bufInfo.commandBufferCount = chainLength;
		if (vkAllocateCommandBuffers(appDevice, &bufInfo, appCommandBuffers.data()) != VK_SUCCESS) {
			OOVR_LOG("vkAllocateCommandBuffers failed: this eye is left out");
			appCommandBuffers.clear();
			swapchainImages.clear();
			xrDestroySwapchain(chain);
			chain = XR_NULL_HANDLE;
			lastResult = XR_ERROR_RUNTIME_FAILURE;
			return;
		}
	}

	// First find the relevant image to render to
	XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	uint32_t currentIndex = 0;
	if (!chain || SwapchainCallFailed(xrAcquireSwapchainImage(chain, &acquireInfo, &currentIndex), "xrAcquireSwapchainImage") || currentIndex >= appCommandBuffers.size())
		return;

	// Wait until the swapchain is ready - this makes sure the compositor isn't writing to it
	// We don't have to pass in currentIndex since it uses the oldest acquired-but-not-waited-on
	// image, so we should be careful with concurrency here.
	XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	if (SwapchainCallFailed(xrWaitSwapchainImage(chain, &waitInfo), "xrWaitSwapchainImage"))
		return;

	const VkCommandBuffer currentCommandBuffer = appCommandBuffers.at(currentIndex);
	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	// A failed Vulkan call still gives the image back, unwritten, so the swapchain isn't stuck
	auto vkFailed = [&](VkResult res, const char* what) {
		if (res == VK_SUCCESS)
			return false;
		OOVR_LOGF("%s failed: %d (this eye is left out)", what, (int)res);
		lastResult = XR_ERROR_RUNTIME_FAILURE;
		xrReleaseSwapchainImage(chain, &releaseInfo);
		return true;
	};

	if (vkFailed(vkBeginCommandBuffer(currentCommandBuffer, &beginInfo), "vkBeginCommandBuffer"))
		return;

	// transition swapchain image to TRANSFER_DST for copy
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	barrier.image = swapchainImages.at(currentIndex).image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(
	    currentCommandBuffer,
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
	    0,
	    0, nullptr,
	    0, nullptr,
	    1, &barrier);

	uint32_t arrayLayer = 0;
	if (submitFlags & vr::Submit_VulkanTextureWithArrayData) {
		auto tex = static_cast<vr::VRVulkanTextureArrayData_t*>(texture->handle);
		arrayLayer = tex->m_unArrayIndex;
	}
	if (bounds) {
		VkImageBlit region = {};
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = arrayLayer;
		region.srcSubresource.layerCount = 1;
		region.srcOffsets[0] = { (int)(bounds->uMin * tex->m_nWidth), (int)(bounds->vMin * tex->m_nHeight), 0 };
		region.srcOffsets[1] = { (int)(bounds->uMax * tex->m_nWidth), (int)(bounds->vMax * tex->m_nHeight), 1 };
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.mipLevel = 0;
		region.dstSubresource.baseArrayLayer = 0;
		region.dstSubresource.layerCount = 1;
		region.dstOffsets[0] = { 0, 0, 0 };
		region.dstOffsets[1] = { (int)tex->m_nWidth, (int)tex->m_nHeight, 1 };
		vkCmdBlitImage( //
		    currentCommandBuffer, // commandbuffer
		    (VkImage)tex->m_nImage, // srcImage
		    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // srcImageLayout
		    swapchainImages.at(currentIndex).image, // dstImage
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // dstImageLayout
		    1, // regionCount
		    &region, // pRegions
		    VK_FILTER_LINEAR);
	} else {
		VkImageCopy region = {};
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = arrayLayer;
		region.srcSubresource.layerCount = 1;
		region.srcOffset = { 0, 0, 0 };
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.mipLevel = 0;
		region.dstSubresource.baseArrayLayer = 0;
		region.dstSubresource.layerCount = 1;
		region.dstOffset = { 0, 0, 0 };
		region.extent = { tex->m_nWidth, tex->m_nHeight, 1 };

		bool image_is_multisampled = xr_main_view(XruEyeLeft).maxSwapchainSampleCount < tex->m_nSampleCount;
		/*
		    if(bounds)
		    {
		        image_is_multisampled = true;
		        region.srcOffset = { bounds->uMin * tex->m_nWidth, bounds->vMin * tex->m_nHeight, 0 };
		        region.dstOffset = { bounds->uMin * tex->m_nWidth, bounds->vMin * tex->m_nHeight, 0 };
		        region.extent = { (bounds->uMax - bounds->uMin)  * tex->m_nWidth, (bounds->vMax - bounds->vMin) * tex->m_nHeight, 1 };
		    }*/

		if (image_is_multisampled) {
			// HACK: As of July 2022 Monado does not support multisampling, so we can't just copy the image.
			// Instead, we do vkCmdResolveImage into the swapchain image. (note, this doesn't support depth textures)
			// Todo - how do we tell which runtimes support multisampling?

			vkCmdResolveImage( //
			    currentCommandBuffer, // commandbuffer
			    (VkImage)tex->m_nImage, // srcImage
			    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // srcImageLayout
			    swapchainImages.at(currentIndex).image, // dstImage
			    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // dstImageLayout
			    1, // regionCount
			    (VkImageResolve*)&region // pRegions
			);
		} else {
			vkCmdCopyImage( //
			    currentCommandBuffer, // commandbuffer
			    (VkImage)tex->m_nImage, // srcImage
			    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // srcImageLayout
			    swapchainImages.at(currentIndex).image, // dstImage
			    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // dstImageLayout
			    1, // regionCount
			    &region // pRegions
			);
		}
	}

	// transition swapchain image back to COLOR_ATTACHMENT_OPTIMAL for runtime
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	vkCmdPipelineBarrier(
	    currentCommandBuffer, //
	    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	    0,
	    0, nullptr,
	    0, nullptr,
	    1, &barrier);

	if (vkFailed(vkEndCommandBuffer(currentCommandBuffer), "vkEndCommandBuffer"))
		return;

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &currentCommandBuffer;

	if (vkFailed(vkQueueSubmit(tex->m_pQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit"))
		return;

	// Release the swapchain - OpenXR will use the last-released image in a swapchain
	SwapchainCallFailed(xrReleaseSwapchainImage(chain, &releaseInfo), "xrReleaseSwapchainImage");
}

void VkCompositor::InvokeCubemap(const vr::Texture_t* textures)
{
	// Not yet: SetSkyboxOverride answers RequestFailed
	OOVR_LOG_ONCE("VkCompositor::InvokeCubemap: Not yet supported!");
	lastResult = XR_ERROR_RUNTIME_FAILURE;
}

bool VkCompositor::CheckChainCompatible(const vr::VRVulkanTextureData_t& tex, const XrSwapchainCreateInfo& chainDesc, vr::EColorSpace colourSpace)
{
	bool usable = true;
#define FAIL(name)                             \
	do {                                       \
		usable = false;                        \
		OOVR_LOG("Resource mismatch: " #name); \
	} while (0)
#define CHECK(name, chainName)           \
	if (tex.name != chainDesc.chainName) \
	FAIL(name)

	CHECK(m_nWidth, width);
	CHECK(m_nHeight, height);
	CHECK(m_nSampleCount, sampleCount);
	switch (colourSpace) {
	case vr::ColorSpace_Auto: {
		if (handle_colorspace_auto((VkFormat)tex.m_nFormat) != chainDesc.format)
			FAIL(m_nFormat);
		break;
	}
	case vr::ColorSpace_Gamma: {
		if (handle_colorspace_gamma((VkFormat)tex.m_nFormat) != chainDesc.format)
			FAIL(m_nFormat);
		break;
	}
	case vr::ColorSpace_Linear: {
		if (handle_colorspace_linear((VkFormat)tex.m_nFormat) != chainDesc.format)
			FAIL(m_nFormat);
		break;
	}
	default:
		OOVR_LOGF("Invalid colorspace given: %d", colourSpace);
		usable = false;
		break;
	}

#undef CHECK
#undef FAIL

	return usable;
}
#endif
