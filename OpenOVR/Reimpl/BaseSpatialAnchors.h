#pragma once
#include "../BaseCommon.h"

#include <map>
#include <mutex>
#include <string>

// IVRSpatialAnchors_001 declares its pose in its own namespace
struct OOVR_SpatialAnchorPose_t {
	vr::HmdMatrix34_t mAnchorToAbsoluteTracking;
};

/**
 * Anchors kept for this process: a pose in the seated or standing space, as SteamVR's are created. A descriptor holds
 * the pose and its space, so it can be saved and given back later; SteamVR's own descriptors aren't readable here
 * (InvalidArgument, as SteamVR answers a descriptor it can't read).
 */
class BaseSpatialAnchors {
public:
	vr::EVRSpatialAnchorError CreateSpatialAnchorFromDescriptor(const char* pchDescriptor, vr::SpatialAnchorHandle_t* pHandleOut);
	vr::EVRSpatialAnchorError CreateSpatialAnchorFromPose(vr::TrackedDeviceIndex_t unDeviceIndex, vr::ETrackingUniverseOrigin eOrigin,
	    OOVR_SpatialAnchorPose_t* pPose, vr::SpatialAnchorHandle_t* pHandleOut);
	vr::EVRSpatialAnchorError GetSpatialAnchorPose(vr::SpatialAnchorHandle_t unHandle, vr::ETrackingUniverseOrigin eOrigin,
	    OOVR_SpatialAnchorPose_t* pPoseOut);
	vr::EVRSpatialAnchorError GetSpatialAnchorDescriptor(vr::SpatialAnchorHandle_t unHandle, VR_OUT_STRING() char* pchDescriptorOut,
	    uint32_t* punDescriptorBufferLenInOut);

private:
	struct Anchor {
		vr::ETrackingUniverseOrigin origin;
		vr::HmdMatrix34_t pose;
	};
	vr::EVRSpatialAnchorError Add(const Anchor& anchor, vr::SpatialAnchorHandle_t* pHandleOut);

	std::mutex lock;
	std::map<vr::SpatialAnchorHandle_t, Anchor> anchors;
	vr::SpatialAnchorHandle_t next = 0x10000;
};
