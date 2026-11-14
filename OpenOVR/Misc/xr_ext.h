//
// Created by ZNix on 25/10/2020.
//

#pragma once

// TODO Turtle1331 it would be nicer to include OpenXR-SDK's xr_dependencies.h if possible
#if defined(SUPPORT_DX) && defined(SUPPORT_DX11)
#include <d3d11.h>
#endif

#if defined(SUPPORT_DX) && defined(SUPPORT_DX12)
#include <d3d12.h>
#endif

#ifdef SUPPORT_GL
#include <GL/gl.h>

#ifndef _WIN32
// Currently just support XLIB
#include <GL/glx.h>
#endif

#endif

#ifdef SUPPORT_GLES
#include <EGL/egl.h>
#endif

#ifdef SUPPORT_VK
#include <vulkan/vulkan.h>
#endif

#ifdef ANDROID
#include <jni.h>
#endif

#include "../logging.h"

#include "../RuntimeExtensions/XR_MNDX_xdev_space.h"
#include <openxr/openxr_platform.h>

#include <vector>

typedef uint32_t XrGraphicsApiSupportedFlags;

// Flag bits for supported graphics apis
static const XrGraphicsApiSupportedFlags XR_SUPPORTED_GRAPHICS_API_D3D11 = 0x0001;
static const XrGraphicsApiSupportedFlags XR_SUPPORTED_GRAPHICS_API_D3D12 = 0x0002;
static const XrGraphicsApiSupportedFlags XR_SUPPORTED_GRAPHICS_API_GL = 0x0004;
static const XrGraphicsApiSupportedFlags XR_SUPPORTED_GRAPHICS_API_GLES = 0x0008;
static const XrGraphicsApiSupportedFlags XR_SUPPORTED_GRAPHICS_API_VK = 0x0010;

/**
 * A wrapper class for the function pointers to OpenXR extensions.
 *
 * Modern versions of the OpenXR SDK don't provide implementations of the extension
 * methods, and we have to use xrGetInstanceProcAddr to get said function pointers
 * manually. One global instance of XrExt is defined in xrutil.h as xr_ext which is
 * setup when the OpenXR instance is created.
 *
 * See https://github.com/microsoft/OpenXR-MixedReality/issues/32
 */
class XrExt {
public:
	XrExt(XrGraphicsApiSupportedFlags apis, const std::vector<const char*>& extensions);

	bool G2Controller_Available() { return supportsG2Controller; }
	bool CylinderKHR_Available() { return supportsCylinderKHR; }
	bool xrGetVisibilityMaskKHR_Available() { return pfnXrGetVisibilityMaskKHR != nullptr; }
	bool xrMndxXdevSpace_Available()
	{
		return pfnxrCreateXDevListMNDX && pfnxrGetXDevListGenerationNumberMNDX && pfnxrEnumerateXDevsMNDX
		    && pfnxrGetXDevPropertiesMNDX && pfnxrDestroyXDevListMNDX && pfnxrCreateXDevSpaceMNDX;
	}

	XrResult xrGetVisibilityMaskKHR(
	    XrSession session,
	    XrViewConfigurationType viewConfigurationType,
	    uint32_t viewIndex,
	    XrVisibilityMaskTypeKHR visibilityMaskType,
	    XrVisibilityMaskKHR* visibilityMask)
	{
		if (!pfnXrGetVisibilityMaskKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetVisibilityMaskKHR(session, viewConfigurationType, viewIndex, visibilityMaskType, visibilityMask);
	}

	bool handTrackingExtensionAvailable() { return pfnXrCreateHandTrackerExt != nullptr; }
	XrResult xrCreateHandTrackerEXT(XrSession session, const XrHandTrackerCreateInfoEXT* createInfo, XrHandTrackerEXT* handTracker)
	{
		if (!pfnXrCreateHandTrackerExt)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrCreateHandTrackerExt(session, createInfo, handTracker);
	}
	XrResult xrDestroyHandTrackerEXT(XrHandTrackerEXT handTracker)
	{
		if (!pfnXrDestroyHandTrackerExt)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrDestroyHandTrackerExt(handTracker);
	}
	XrResult xrLocateHandJointsEXT(XrHandTrackerEXT handTracker, const XrHandJointsLocateInfoEXT* locateInfo, XrHandJointLocationsEXT* locations)
	{
		if (!pfnXrLocateHandJointsExt)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrLocateHandJointsExt(handTracker, locateInfo, locations);
	}

	XrResult xrCreateXDevListMNDX(XrSession session, const XrCreateXDevListInfoMNDX* createInfo, XrXDevListMNDX* xdevList)
	{
		if (!pfnxrCreateXDevListMNDX)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnxrCreateXDevListMNDX(session, createInfo, xdevList);
	}

	XrResult xrGetXDevListGenerationNumberMNDX(XrXDevListMNDX xdevList, uint64_t* outGeneration)
	{
		if (!pfnxrGetXDevListGenerationNumberMNDX)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnxrGetXDevListGenerationNumberMNDX(xdevList, outGeneration);
	}

	XrResult xrEnumerateXDevsMNDX(XrXDevListMNDX xdevList, uint32_t xdevCapacityInput, uint32_t* xdevCountOutput, XrXDevIdMNDX* xdevs)
	{
		if (!pfnxrEnumerateXDevsMNDX)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnxrEnumerateXDevsMNDX(xdevList, xdevCapacityInput, xdevCountOutput, xdevs);
	}

	XrResult xrGetXDevPropertiesMNDX(XrXDevListMNDX xdevList, const XrGetXDevInfoMNDX* info, XrXDevPropertiesMNDX* properties)
	{
		if (!pfnxrGetXDevPropertiesMNDX)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnxrGetXDevPropertiesMNDX(xdevList, info, properties);
	}

	XrResult xrDestroyXDevListMNDX(XrXDevListMNDX xdevList)
	{
		if (!pfnxrDestroyXDevListMNDX)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnxrDestroyXDevListMNDX(xdevList);
	}

	XrResult xrCreateXDevSpaceMNDX(XrSession session, const XrCreateXDevSpaceInfoMNDX* createInfo, XrSpace* space)
	{
		if (!pfnxrCreateXDevSpaceMNDX)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnxrCreateXDevSpaceMNDX(session, createInfo, space);
	}

#if defined(SUPPORT_DX) && defined(SUPPORT_DX11)
	bool xrGetD3D11GraphicsRequirementsKHR_Available()
	{
		return pfnXrGetD3D11GraphicsRequirementsKHR != nullptr;
	}
	XrResult xrGetD3D11GraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsD3D11KHR* graphicsRequirements)
	{
		if (!pfnXrGetD3D11GraphicsRequirementsKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetD3D11GraphicsRequirementsKHR(instance, systemId, graphicsRequirements);
	}
#endif
#if defined(SUPPORT_DX) && defined(SUPPORT_DX12)
	bool xrGetD3D12GraphicsRequirementsKHR_Available()
	{
		return pfnXrGetD3D12GraphicsRequirementsKHR != nullptr;
	}
	XrResult xrGetD3D12GraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsD3D12KHR* graphicsRequirements)
	{
		if (!pfnXrGetD3D12GraphicsRequirementsKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetD3D12GraphicsRequirementsKHR(instance, systemId, graphicsRequirements);
	}
#endif
#ifdef SUPPORT_VK
	bool xrGetVulkanInstanceExtensionsKHR_Available()
	{
		return pfnXrGetVulkanInstanceExtensionsKHR != nullptr;
	}
	XrResult xrGetVulkanInstanceExtensionsKHR(XrInstance instance, XrSystemId systemId, uint32_t bufferCapacityInput, uint32_t* bufferCountOutput, char* buffer)
	{
		if (!pfnXrGetVulkanInstanceExtensionsKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetVulkanInstanceExtensionsKHR(instance, systemId, bufferCapacityInput, bufferCountOutput, buffer);
	}
	bool xrGetVulkanDeviceExtensionsKHR_Available() { return pfnXrGetVulkanDeviceExtensionsKHR != nullptr; }
	XrResult xrGetVulkanDeviceExtensionsKHR(XrInstance instance, XrSystemId systemId, uint32_t bufferCapacityInput, uint32_t* bufferCountOutput, char* buffer)
	{
		if (!pfnXrGetVulkanDeviceExtensionsKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetVulkanDeviceExtensionsKHR(instance, systemId, bufferCapacityInput, bufferCountOutput, buffer);
	}
	bool xrGetVulkanGraphicsDeviceKHR_Available() { return pfnXrGetVulkanGraphicsDeviceKHR != nullptr; }
	XrResult xrGetVulkanGraphicsDeviceKHR(XrInstance instance, XrSystemId systemId, VkInstance vkInstance, VkPhysicalDevice* vkPhysicalDevice)
	{
		if (!pfnXrGetVulkanGraphicsDeviceKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetVulkanGraphicsDeviceKHR(instance, systemId, vkInstance, vkPhysicalDevice);
	}
	bool xrGetVulkanGraphicsRequirementsKHR_Available() { return pfnXrGetVulkanGraphicsRequirementsKHR != nullptr; }
	XrResult xrGetVulkanGraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsVulkanKHR* graphicsRequirements)
	{
		if (!pfnXrGetVulkanGraphicsRequirementsKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetVulkanGraphicsRequirementsKHR(instance, systemId, graphicsRequirements);
	}
#endif
#ifdef SUPPORT_GL
	bool xrGetOpenGLGraphicsRequirementsKHR_Available()
	{
		return pfnXrGetOpenGLGraphicsRequirementsKHR != nullptr;
	}
	XrResult xrGetOpenGLGraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsOpenGLKHR* graphicsRequirements)
	{
		if (!pfnXrGetOpenGLGraphicsRequirementsKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetOpenGLGraphicsRequirementsKHR(instance, systemId, graphicsRequirements);
	}
#endif
#ifdef SUPPORT_GLES
	bool xrGetOpenGLESGraphicsRequirementsKHR_Available()
	{
		return pfnXrGetOpenGLESGraphicsRequirementsKHR != nullptr;
	}
	XrResult xrGetOpenGLESGraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsOpenGLESKHR* graphicsRequirements)
	{
		if (!pfnXrGetOpenGLESGraphicsRequirementsKHR)
			return XR_ERROR_FUNCTION_UNSUPPORTED; // the runtime lacks the extension; callers check the result
		return pfnXrGetOpenGLESGraphicsRequirementsKHR(instance, systemId, graphicsRequirements);
	}
#endif

private:
	PFN_xrGetVisibilityMaskKHR pfnXrGetVisibilityMaskKHR = nullptr;
	PFN_xrCreateHandTrackerEXT pfnXrCreateHandTrackerExt = nullptr;
	PFN_xrDestroyHandTrackerEXT pfnXrDestroyHandTrackerExt = nullptr;
	PFN_xrLocateHandJointsEXT pfnXrLocateHandJointsExt = nullptr;

	PFN_xrCreateXDevListMNDX pfnxrCreateXDevListMNDX = nullptr;
	PFN_xrGetXDevListGenerationNumberMNDX pfnxrGetXDevListGenerationNumberMNDX = nullptr;
	PFN_xrEnumerateXDevsMNDX pfnxrEnumerateXDevsMNDX = nullptr;
	PFN_xrGetXDevPropertiesMNDX pfnxrGetXDevPropertiesMNDX = nullptr;
	PFN_xrDestroyXDevListMNDX pfnxrDestroyXDevListMNDX = nullptr;
	PFN_xrCreateXDevSpaceMNDX pfnxrCreateXDevSpaceMNDX = nullptr;

	bool supportsG2Controller = false;
	bool supportsCylinderKHR = false;

#if defined(SUPPORT_DX) && defined(SUPPORT_DX11)
	PFN_xrGetD3D11GraphicsRequirementsKHR pfnXrGetD3D11GraphicsRequirementsKHR = nullptr;
#endif
#if defined(SUPPORT_DX) && defined(SUPPORT_DX12)
	PFN_xrGetD3D12GraphicsRequirementsKHR pfnXrGetD3D12GraphicsRequirementsKHR = nullptr;
#endif
#ifdef SUPPORT_VK
	PFN_xrGetVulkanGraphicsRequirementsKHR pfnXrGetVulkanGraphicsRequirementsKHR = nullptr;
	PFN_xrGetVulkanInstanceExtensionsKHR pfnXrGetVulkanInstanceExtensionsKHR = nullptr;
	PFN_xrGetVulkanDeviceExtensionsKHR pfnXrGetVulkanDeviceExtensionsKHR = nullptr;
	PFN_xrGetVulkanGraphicsDeviceKHR pfnXrGetVulkanGraphicsDeviceKHR = nullptr;
#endif
#ifdef SUPPORT_GL
	PFN_xrGetOpenGLGraphicsRequirementsKHR pfnXrGetOpenGLGraphicsRequirementsKHR = nullptr;
#endif
#ifdef SUPPORT_GLES
	PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnXrGetOpenGLESGraphicsRequirementsKHR = nullptr;
#endif
};

// Put this here rather tha xrutil.h so not all files have to include vector
extern std::vector<XrViewConfigurationView> xr_views_list;
