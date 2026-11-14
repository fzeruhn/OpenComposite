#include "stdafx.h"
#define BASE_IMPL
#include "BaseTrackedCamera.h"

#include "Misc/EnumNames.h"

const char* BaseTrackedCamera::GetCameraErrorNameFromEnum(vr::EVRTrackedCameraError eCameraError)
{
	// SteamVR names an unknown value without the E
	return OC_EnumNameAsSteamVR("EVRTrackedCameraError", (int32_t)eCameraError, "Unknown VRTrackedCameraError (%d)");
}

BaseTrackedCamera::ForDeviceAnswer BaseTrackedCamera::ForDevice(vr::TrackedDeviceIndex_t nDeviceIndex, bool badFrameType)
{
	if (nDeviceIndex == vr::k_unTrackedDeviceIndexInvalid)
		return { vr::VRTrackedCameraError_NotSupportedForThisDevice };
	if (nDeviceIndex >= vr::k_unMaxTrackedDeviceCount || badFrameType)
		return { vr::VRTrackedCameraError_OperationFailed };
	return { vr::VRTrackedCameraError_InvalidArgument };
}

vr::EVRTrackedCameraError BaseTrackedCamera::HasCamera(vr::TrackedDeviceIndex_t nDeviceIndex, bool* pHasCamera)
{
	if (pHasCamera)
		*pHasCamera = false;
	if (nDeviceIndex == vr::k_unTrackedDeviceIndexInvalid)
		return vr::VRTrackedCameraError_NotSupportedForThisDevice;
	return vr::VRTrackedCameraError_None;
}

bool BaseTrackedCamera::HasCamera(vr::TrackedDeviceIndex_t nDeviceIndex)
{
	return false;
}

vr::EVRTrackedCameraError BaseTrackedCamera::GetCameraFrameSize(vr::TrackedDeviceIndex_t nDeviceIndex, vr::EVRTrackedCameraFrameType eFrameType,
    uint32_t* pnWidth, uint32_t* pnHeight, uint32_t* pnFrameBufferSize)
{
	if (pnWidth)
		*pnWidth = 0;
	if (pnHeight)
		*pnHeight = 0;
	if (pnFrameBufferSize)
		*pnFrameBufferSize = 0;
	if (nDeviceIndex == vr::k_unTrackedDeviceIndexInvalid)
		return vr::VRTrackedCameraError_NotSupportedForThisDevice;
	return vr::VRTrackedCameraError_OperationFailed;
}

vr::EVRTrackedCameraError BaseTrackedCamera::GetVideoStreamTextureSize(vr::TrackedDeviceIndex_t nDeviceIndex, vr::EVRTrackedCameraFrameType eFrameType,
    vr::VRTextureBounds_t* pTextureBounds, uint32_t* pnWidth, uint32_t* pnHeight)
{
	if (pTextureBounds)
		*pTextureBounds = {};
	if (pnWidth)
		*pnWidth = 0;
	if (pnHeight)
		*pnHeight = 0;
	if (nDeviceIndex == vr::k_unTrackedDeviceIndexInvalid)
		return vr::VRTrackedCameraError_NotSupportedForThisDevice;
	// SteamVR answers None for a frame type it doesn't know here, with nothing filled in
	if ((uint32_t)eFrameType >= vr::MAX_CAMERA_FRAME_TYPES)
		return vr::VRTrackedCameraError_None;
	return vr::VRTrackedCameraError_OperationFailed;
}

vr::EVRTrackedCameraError BaseTrackedCamera::AcquireVideoStreamingService(vr::TrackedDeviceIndex_t nDeviceIndex, vr::TrackedCameraHandle_t* pHandle)
{
	if (pHandle)
		*pHandle = INVALID_TRACKED_CAMERA_HANDLE;
	if (nDeviceIndex == vr::k_unTrackedDeviceIndexInvalid)
		return vr::VRTrackedCameraError_NotSupportedForThisDevice;
	return vr::VRTrackedCameraError_OperationFailed;
}

vr::EVRTrackedCameraError BaseTrackedCamera::ReleaseVideoStreamTextureGL(vr::TrackedCameraHandle_t hTrackedCamera, vr::glUInt_t glTextureId)
{
	if (glTextureId == 0)
		return vr::VRTrackedCameraError_InvalidGLTextureId;
	return vr::VRTrackedCameraError_InvalidHandle; // no streaming handle is ever handed out
}

vr::ECameraVideoStreamFormat BaseTrackedCamera::GetCameraVideoStreamFormat(vr::TrackedDeviceIndex_t nDeviceIndex)
{
	return vr::CVS_FORMAT_UNKNOWN;
}

void BaseTrackedCamera::SetCameraTrackingSpace(vr::ETrackingUniverseOrigin eUniverse)
{
	trackingSpace = eUniverse;
}

vr::ETrackingUniverseOrigin BaseTrackedCamera::GetCameraTrackingSpace()
{
	return trackingSpace;
}
