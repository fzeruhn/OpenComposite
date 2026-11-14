#include "stdafx.h"
#define BASE_IMPL
#include "BaseSpatialAnchors.h"

#include "BaseSystem.h"
#include "convert.h"
#include "generated/static_bases.gen.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

using vr::VRSpatialAnchorError_ArrayTooSmall;
using vr::VRSpatialAnchorError_InvalidArgument;
using vr::VRSpatialAnchorError_Success;
using vr::VRSpatialAnchorError_UnknownHandle;

static const char* const kDescriptorPrefix = "OpenComposite-anchor";

static bool UsableOrigin(vr::ETrackingUniverseOrigin origin)
{
	return origin == vr::TrackingUniverseSeated || origin == vr::TrackingUniverseStanding || origin == vr::TrackingUniverseRawAndUncalibrated;
}

static bool Finite(const vr::HmdMatrix34_t& m)
{
	for (const auto& row : m.m)
		for (float v : row)
			if (!std::isfinite(v))
				return false;
	return true;
}

// The pose moved from one space to another; raw and uncalibrated counts as standing
static vr::HmdMatrix34_t Convert(const vr::HmdMatrix34_t& pose, vr::ETrackingUniverseOrigin from, vr::ETrackingUniverseOrigin to)
{
	bool fromSeated = from == vr::TrackingUniverseSeated, toSeated = to == vr::TrackingUniverseSeated;
	BaseSystem* system = GetUnsafeBaseSystem();
	if (fromSeated == toSeated || !system)
		return pose;
	glm::mat4 seatedToStanding = S2G_m34(system->GetSeatedZeroPoseToStandingAbsoluteTrackingPose());
	glm::mat4 m = S2G_m34(pose);
	return G2S_m34(fromSeated ? seatedToStanding * m : glm::inverse(seatedToStanding) * m);
}

vr::EVRSpatialAnchorError BaseSpatialAnchors::Add(const Anchor& anchor, vr::SpatialAnchorHandle_t* pHandleOut)
{
	std::lock_guard<std::mutex> guard(lock);
	vr::SpatialAnchorHandle_t handle = next++;
	anchors[handle] = anchor;
	*pHandleOut = handle;
	return VRSpatialAnchorError_Success;
}

vr::EVRSpatialAnchorError BaseSpatialAnchors::CreateSpatialAnchorFromDescriptor(const char* pchDescriptor, vr::SpatialAnchorHandle_t* pHandleOut)
{
	if (pHandleOut)
		*pHandleOut = 0; // k_ulInvalidSpatialAnchorHandle
	if (!pchDescriptor || !pHandleOut)
		return VRSpatialAnchorError_InvalidArgument;

	// "<prefix> <origin> <12 floats>", as GetSpatialAnchorDescriptor writes it
	std::istringstream in(pchDescriptor);
	in.imbue(std::locale::classic()); // a game's setlocale doesn't change what a descriptor means
	std::string prefix;
	int origin = -1;
	Anchor anchor = {};
	in >> prefix >> origin;
	for (auto& row : anchor.pose.m)
		for (float& v : row)
			in >> v;
	anchor.origin = (vr::ETrackingUniverseOrigin)origin;
	if (!in || prefix != kDescriptorPrefix || !UsableOrigin(anchor.origin) || !Finite(anchor.pose))
		return VRSpatialAnchorError_InvalidArgument;
	return Add(anchor, pHandleOut);
}

vr::EVRSpatialAnchorError BaseSpatialAnchors::CreateSpatialAnchorFromPose(vr::TrackedDeviceIndex_t unDeviceIndex,
    vr::ETrackingUniverseOrigin eOrigin, OOVR_SpatialAnchorPose_t* pPose, vr::SpatialAnchorHandle_t* pHandleOut)
{
	if (pHandleOut)
		*pHandleOut = 0;
	if (!pPose || !pHandleOut || unDeviceIndex >= vr::k_unMaxTrackedDeviceCount || !UsableOrigin(eOrigin)
	    || !Finite(pPose->mAnchorToAbsoluteTracking))
		return VRSpatialAnchorError_InvalidArgument;
	return Add({ eOrigin, pPose->mAnchorToAbsoluteTracking }, pHandleOut);
}

vr::EVRSpatialAnchorError BaseSpatialAnchors::GetSpatialAnchorPose(vr::SpatialAnchorHandle_t unHandle, vr::ETrackingUniverseOrigin eOrigin,
    OOVR_SpatialAnchorPose_t* pPoseOut)
{
	Anchor anchor;
	{
		std::lock_guard<std::mutex> guard(lock);
		auto it = anchors.find(unHandle);
		if (it == anchors.end())
			return VRSpatialAnchorError_UnknownHandle;
		anchor = it->second;
	}
	if (!pPoseOut || !UsableOrigin(eOrigin))
		return VRSpatialAnchorError_InvalidArgument;
	pPoseOut->mAnchorToAbsoluteTracking = Convert(anchor.pose, anchor.origin, eOrigin);
	return VRSpatialAnchorError_Success;
}

vr::EVRSpatialAnchorError BaseSpatialAnchors::GetSpatialAnchorDescriptor(vr::SpatialAnchorHandle_t unHandle, char* pchDescriptorOut,
    uint32_t* punDescriptorBufferLenInOut)
{
	if (pchDescriptorOut && punDescriptorBufferLenInOut && *punDescriptorBufferLenInOut)
		pchDescriptorOut[0] = '\0';
	Anchor anchor;
	{
		std::lock_guard<std::mutex> guard(lock);
		auto it = anchors.find(unHandle);
		if (it == anchors.end())
			return VRSpatialAnchorError_UnknownHandle;
		anchor = it->second;
	}
	if (!punDescriptorBufferLenInOut)
		return VRSpatialAnchorError_InvalidArgument;

	// Written in the classic locale, as CreateSpatialAnchorFromDescriptor reads it, whatever the game's setlocale says
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out.precision(9);
	out << kDescriptorPrefix << " " << (int)anchor.origin;
	for (const auto& row : anchor.pose.m)
		for (float v : row)
			out << " " << v;
	std::string descriptor = out.str();
	uint32_t needed = (uint32_t)descriptor.size() + 1;
	bool fits = pchDescriptorOut && *punDescriptorBufferLenInOut >= needed;
	*punDescriptorBufferLenInOut = needed;
	if (!fits)
		return VRSpatialAnchorError_ArrayTooSmall;
	memcpy(pchDescriptorOut, descriptor.c_str(), needed);
	return VRSpatialAnchorError_Success;
}
