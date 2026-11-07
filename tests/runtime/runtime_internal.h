#pragma once
// Internals of the stand-in OpenXR runtime (oc_test_runtime).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>

// The project defines every graphics API for every target; this runtime only does D3D11
#undef XR_USE_GRAPHICS_API_VULKAN
#undef XR_USE_GRAPHICS_API_OPENGL
#undef XR_USE_GRAPHICS_API_OPENGL_ES
#undef XR_USE_GRAPHICS_API_D3D12
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <loader_interfaces.h>

#include "script.h"

#include <cstdint>
#include <string>
#include <vector>

struct RtSwapchain {
	XrSwapchainCreateInfo info{};
	std::vector<ID3D11Texture2D*> textures;
	uint32_t next = 0;
	int acquired = 0;
};

struct RtSession {
	ID3D11Device* device = nullptr;
	bool headless = false;
	bool overlay = false;
	bool running = false;
	XrSessionState state = XR_SESSION_STATE_UNKNOWN;
	uint64_t framesWaited = 0;
	uint64_t framesBegun = 0;
	uint64_t framesEnded = 0;
	bool frameOpen = false;
};

// swapchain_d3d11.cpp
XrResult RtD3D11Requirements(XrGraphicsRequirementsD3D11KHR* reqs);
XrResult RtD3D11CreateSwapchain(RtSession* session, const XrSwapchainCreateInfo* info, RtSwapchain* sc);
void RtD3D11DestroySwapchain(RtSwapchain* sc);
const std::vector<int64_t>& RtD3D11Formats();
