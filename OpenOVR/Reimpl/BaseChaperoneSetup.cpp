#include "stdafx.h"

#define BASE_IMPL
#include "BaseChaperoneSetup.h"

#include "convert.h"

#include <string>

using namespace vr;

bool BaseChaperoneSetup::CommitWorkingCopy(EChaperoneConfigFile configFile)
{
	// The OpenXR runtime owns the room setup; nothing can be written to it
	OC_UNSUPPORTED(false, "CommitWorkingCopy: the room setup is the OpenXR runtime's");
}
void BaseChaperoneSetup::RevertWorkingCopy()
{
	// The OpenXR runtime owns the room setup; there's no working copy to change
}
bool BaseChaperoneSetup::GetWorkingPlayAreaSize(float* pSizeX, float* pSizeZ)
{
	if (pSizeX)
		*pSizeX = 0;
	if (pSizeZ)
		*pSizeZ = 0;
	return false;
}
bool BaseChaperoneSetup::GetWorkingPlayAreaRect(HmdQuad_t* rect)
{
	if (rect)
		*rect = {};
	return false;
}
bool BaseChaperoneSetup::GetWorkingCollisionBoundsInfo(VR_OUT_ARRAY_COUNT(punQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t* punQuadsCount)
{
	if (punQuadsCount)
		*punQuadsCount = 0;
	return false;
}
bool BaseChaperoneSetup::GetLiveCollisionBoundsInfo(VR_OUT_ARRAY_COUNT(punQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t* punQuadsCount)
{
	using glm::vec3;

	// TODO better find out what this method does

	XrExtent2Df bounds;
	XrResult result = xrGetReferenceSpaceBoundsRect(xr_session.get(), XR_REFERENCE_SPACE_TYPE_STAGE, &bounds);

	if (result == XR_SPACE_BOUNDS_UNAVAILABLE) {
		if (punQuadsCount)
			*punQuadsCount = 0;
		return false; // TODO verify SteamVR returns this if Guardian isn't set up
	}

	if (XR_FAILED(result)) {
		if (punQuadsCount)
			*punQuadsCount = 0;
		OC_UNSUPPORTED(false, "GetLiveCollisionBoundsInfo: xrGetReferenceSpaceBoundsRect failed (%d)", (int)result);
	}

	if (pQuadsBuffer) {
		HmdVector3_t* corners = pQuadsBuffer->vCorners;
		// TODO is this correct? Surely it's offset a bit? What happens when you recentre?
		corners[0] = G2S_v3f(vec3(-bounds.width, 0, -bounds.height));
		corners[1] = G2S_v3f(vec3(-bounds.width, 0, bounds.height));
		corners[2] = G2S_v3f(vec3(bounds.width, 0, bounds.height));
		corners[3] = G2S_v3f(vec3(bounds.width, 0, -bounds.height));
	}

	// string msg = to_string(status) + "," + to_string(pointsCount);
	// OOVR_LOG(msg.c_str());

	if (punQuadsCount)
		*punQuadsCount = 1;

	return true;
}
bool BaseChaperoneSetup::GetWorkingSeatedZeroPoseToRawTrackingPose(HmdMatrix34_t* pmatSeatedZeroPoseToRawTrackingPose)
{
	if (pmatSeatedZeroPoseToRawTrackingPose)
		*pmatSeatedZeroPoseToRawTrackingPose = {};
	return false;
}
bool BaseChaperoneSetup::GetWorkingStandingZeroPoseToRawTrackingPose(HmdMatrix34_t* pmatStandingZeroPoseToRawTrackingPose)
{
	if (pmatStandingZeroPoseToRawTrackingPose)
		*pmatStandingZeroPoseToRawTrackingPose = {};
	return false;
}
void BaseChaperoneSetup::SetWorkingPlayAreaSize(float sizeX, float sizeZ)
{
	// Called by VRGIN (a VR mod framework), to hide Chaperone during seated play. Noop here.
}
void BaseChaperoneSetup::SetWorkingCollisionBoundsInfo(VR_ARRAY_COUNT(unQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t unQuadsCount)
{
	// The OpenXR runtime owns the room setup; there's no working copy to change
}
void BaseChaperoneSetup::SetWorkingSeatedZeroPoseToRawTrackingPose(const HmdMatrix34_t* pMatSeatedZeroPoseToRawTrackingPose)
{
	// The OpenXR runtime owns the room setup; there's no working copy to change
}
void BaseChaperoneSetup::SetWorkingStandingZeroPoseToRawTrackingPose(const HmdMatrix34_t* pMatStandingZeroPoseToRawTrackingPose)
{
	// The OpenXR runtime owns the room setup; there's no working copy to change
}
void BaseChaperoneSetup::ReloadFromDisk(EChaperoneConfigFile configFile)
{
	// The play area is asked of the OpenXR runtime each time
}
bool BaseChaperoneSetup::GetLiveSeatedZeroPoseToRawTrackingPose(HmdMatrix34_t* pmatSeatedZeroPoseToRawTrackingPose)
{
	if (pmatSeatedZeroPoseToRawTrackingPose)
		*pmatSeatedZeroPoseToRawTrackingPose = {};
	return false;
}
void BaseChaperoneSetup::SetWorkingCollisionBoundsTagsInfo(VR_ARRAY_COUNT(unTagCount) uint8_t* pTagsBuffer, uint32_t unTagCount)
{
	// The OpenXR runtime owns the room setup; there's no working copy to change
}
bool BaseChaperoneSetup::GetLiveCollisionBoundsTagsInfo(VR_OUT_ARRAY_COUNT(punTagCount) uint8_t* pTagsBuffer, uint32_t* punTagCount)
{
	if (punTagCount)
		*punTagCount = 0;
	return false;
}
bool BaseChaperoneSetup::SetWorkingPhysicalBoundsInfo(VR_ARRAY_COUNT(unQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t unQuadsCount)
{
	// The OpenXR runtime owns the room setup; there's no working copy to change
	return false;
}
bool BaseChaperoneSetup::GetLivePhysicalBoundsInfo(VR_OUT_ARRAY_COUNT(punQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t* punQuadsCount)
{
	if (punQuadsCount)
		*punQuadsCount = 0;
	return false;
}
bool BaseChaperoneSetup::ExportLiveToBuffer(VR_OUT_STRING() char* pBuffer, uint32_t* pnBufferLength)
{
	if (pBuffer && pnBufferLength && *pnBufferLength)
		pBuffer[0] = 0;
	if (pnBufferLength)
		*pnBufferLength = 0;
	return false;
}
bool BaseChaperoneSetup::ImportFromBufferToWorking(const char* pBuffer, uint32_t nImportFlags)
{
	// The OpenXR runtime owns the room setup; nothing can be imported into it
	return false;
}
void BaseChaperoneSetup::SetWorkingPerimeter(VR_ARRAY_COUNT(unPointCount) const HmdVector2_t* pPointBuffer, uint32_t unPointCount)
{
	// The OpenXR runtime owns the room setup; there's no working copy to change
}
void BaseChaperoneSetup::ShowWorkingSetPreview()
{
	// No working copy to preview
}
void BaseChaperoneSetup::HideWorkingSetPreview()
{
	// No working copy to preview
}
void BaseChaperoneSetup::RoomSetupStarting()
{
	// The OpenXR runtime has its own room setup
}

void BaseChaperoneSetup::SetWorkingWallTagInfo(VR_ARRAY_COUNT(unTagCount) uint8_t* pTagsBuffer, uint32_t unTagCount)
{
	SetWorkingCollisionBoundsTagsInfo(pTagsBuffer, unTagCount);
}
bool BaseChaperoneSetup::GetLiveWallTagInfo(VR_OUT_ARRAY_COUNT(punTagCount) uint8_t* pTagsBuffer, uint32_t* punTagCount)
{
	return GetLiveCollisionBoundsTagsInfo(pTagsBuffer, punTagCount);
}
