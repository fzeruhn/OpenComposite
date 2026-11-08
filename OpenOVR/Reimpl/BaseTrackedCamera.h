#pragma once
#include "../BaseCommon.h"
#include "generated/interfaces/vrtrackedcameratypes.h"

#include <type_traits>

/**
 * There's no camera on OpenXR (passthrough is the runtime's own). The calls answer as SteamVR does for a headset
 * without one: HasCamera false; an invalid device index NotSupportedForThisDevice; a call on a streaming handle
 * InvalidHandle (none is ever handed out); the rest OperationFailed or InvalidArgument as SteamVR picks.
 *
 * The versions' signatures differ from each other, so the calls that only answer take any arguments; those of
 * IVRTrackedCamera_001 that return no error answer false, 0 or null.
 */
class BaseTrackedCamera {
public:
	// The answer of a call: E where it returns an error, else false, 0 or null
	template <int E>
	struct Answer {
		template <typename T>
		operator T() const
		{
			if constexpr (std::is_enum_v<T>)
				return (T)E;
			else
				return T{};
		}
	};
	using OperationFailed = Answer<vr::VRTrackedCameraError_OperationFailed>;
	using InvalidHandle = Answer<vr::VRTrackedCameraError_InvalidHandle>;
	using NotSupported = Answer<vr::VRTrackedCameraError_NotSupportedForThisDevice>;
	using InvalidArgument = Answer<vr::VRTrackedCameraError_InvalidArgument>;

	// The answer to a call about one device's camera
	struct ForDeviceAnswer {
		vr::EVRTrackedCameraError error;
		template <typename T>
		operator T() const
		{
			if constexpr (std::is_enum_v<T>)
				return (T)error;
			else
				return T{}; // IVRTrackedCamera_001's bool
		}
	};

	const char* GetCameraErrorNameFromEnum(vr::EVRTrackedCameraError eCameraError);

	/** IVRTrackedCamera_002 on: None with *pHasCamera false; NotSupportedForThisDevice for k_unTrackedDeviceIndexInvalid */
	vr::EVRTrackedCameraError HasCamera(vr::TrackedDeviceIndex_t nDeviceIndex, bool* pHasCamera);
	/** IVRTrackedCamera_001: false */
	bool HasCamera(vr::TrackedDeviceIndex_t nDeviceIndex);

	/** OperationFailed, outputs zeroed */
	vr::EVRTrackedCameraError GetCameraFrameSize(vr::TrackedDeviceIndex_t nDeviceIndex, vr::EVRTrackedCameraFrameType eFrameType,
	    uint32_t* pnWidth, uint32_t* pnHeight, uint32_t* pnFrameBufferSize);
	vr::EVRTrackedCameraError GetVideoStreamTextureSize(vr::TrackedDeviceIndex_t nDeviceIndex, vr::EVRTrackedCameraFrameType eFrameType,
	    vr::VRTextureBounds_t* pTextureBounds, uint32_t* pnWidth, uint32_t* pnHeight);
	vr::EVRTrackedCameraError AcquireVideoStreamingService(vr::TrackedDeviceIndex_t nDeviceIndex, vr::TrackedCameraHandle_t* pHandle);

	/** InvalidArgument, OperationFailed for an unknown frame type; IVRTrackedCamera_001's projection (a bool) false */
	template <typename... Args>
	ForDeviceAnswer GetCameraIntrinsics(vr::TrackedDeviceIndex_t nDeviceIndex, Args... args) { return ForDevice(nDeviceIndex, (false || ... || BadFrameType(args))); }
	template <typename... Args>
	ForDeviceAnswer GetCameraIntrinisics(vr::TrackedDeviceIndex_t nDeviceIndex, Args... args) { return ForDevice(nDeviceIndex, (false || ... || BadFrameType(args))); } // 002's spelling
	template <typename... Args>
	ForDeviceAnswer GetCameraProjection(vr::TrackedDeviceIndex_t nDeviceIndex, Args... args) { return ForDevice(nDeviceIndex, (false || ... || BadFrameType(args))); }

	/** No streaming handle is ever handed out: InvalidHandle */
	template <typename... Args>
	InvalidHandle ReleaseVideoStreamingService(Args...) { return {}; }
	template <typename... Args>
	InvalidHandle GetVideoStreamFrameBuffer(Args...) { return {}; }
	template <typename... Args>
	InvalidHandle GetVideoStreamTextureD3D11(Args...) { return {}; }
	template <typename... Args>
	InvalidHandle GetVideoStreamTextureGL(Args...) { return {}; }

	/** InvalidGLTextureId for texture 0, as SteamVR checks the texture first; InvalidHandle for any other */
	vr::EVRTrackedCameraError ReleaseVideoStreamTextureGL(vr::TrackedCameraHandle_t hTrackedCamera, vr::glUInt_t glTextureId);

	void SetCameraTrackingSpace(vr::ETrackingUniverseOrigin eUniverse);
	vr::ETrackingUniverseOrigin GetCameraTrackingSpace();

	// IVRTrackedCamera_001 (SDK 0.9.11 to 0.9.18): no camera, so false, 0 or null
	vr::ECameraVideoStreamFormat GetCameraVideoStreamFormat(vr::TrackedDeviceIndex_t nDeviceIndex); // CVS_FORMAT_UNKNOWN
	template <typename... Args>
	NotSupported GetCameraFirmwareDescription(Args...) { return {}; }
	template <typename... Args>
	NotSupported GetCameraFrameDimensions(Args...) { return {}; }
	template <typename... Args>
	NotSupported SetCameraVideoStreamFormat(Args...) { return {}; }
	template <typename... Args>
	NotSupported EnableCameraForStreaming(Args...) { return {}; }
	template <typename... Args>
	NotSupported StartVideoStream(Args...) { return {}; }
	template <typename... Args>
	NotSupported StopVideoStream(Args...) { return {}; }
	template <typename... Args>
	NotSupported IsVideoStreamActive(Args...) { return {}; }
	template <typename... Args>
	NotSupported GetVideoStreamElapsedTime(Args...) { return {}; }
	template <typename... Args>
	NotSupported GetVideoStreamFrame(Args...) { return {}; }
	template <typename... Args>
	NotSupported ReleaseVideoStreamFrame(Args...) { return {}; }
	template <typename... Args>
	NotSupported SetAutoExposure(Args...) { return {}; }
	template <typename... Args>
	NotSupported PauseVideoStream(Args...) { return {}; }
	template <typename... Args>
	NotSupported ResumeVideoStream(Args...) { return {}; }
	template <typename... Args>
	NotSupported IsVideoStreamPaused(Args...) { return {}; }
	template <typename... Args>
	NotSupported GetCameraDistortion(Args...) { return {}; }


private:
	static ForDeviceAnswer ForDevice(vr::TrackedDeviceIndex_t nDeviceIndex, bool badFrameType);
	template <typename T>
	static bool BadFrameType(T)
	{
		return false;
	}
	static bool BadFrameType(vr::EVRTrackedCameraFrameType eFrameType) { return (uint32_t)eFrameType >= vr::MAX_CAMERA_FRAME_TYPES; }

	vr::ETrackingUniverseOrigin trackingSpace = vr::TrackingUniverseStanding;
};
