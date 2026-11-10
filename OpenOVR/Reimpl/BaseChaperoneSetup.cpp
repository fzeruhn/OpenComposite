#include "stdafx.h"

#define BASE_IMPL
#include "BaseChaperoneSetup.h"
#include "BaseSystem.h"

#include "Drivers/Backend.h"
#include "Misc/JsonFile.h"
#include "Misc/SharedStore.h"
#include "convert.h"
#include "generated/static_bases.gen.h"

#include <algorithm>
#include <string>

using namespace vr;

// Room setup: a working copy that programs change and commit to the store's Chaperone document, which every
// OpenComposite program then reads. The OpenXR runtime's stage is the room until something is committed. The zero
// poses are kept and reported, not applied: the runtime's own stay in use. The live collision bounds and IVRChaperone's
// play area stay the runtime's: it draws the boundary the user sees.

namespace {

// Copies count items into an app's buffer: with no buffer, only the count; one too small, false with the count needed
template <typename T>
bool CopyArray(const std::vector<T>& items, T* buffer, uint32_t* count)
{
	if (!count)
		return false;
	uint32_t have = *count;
	*count = (uint32_t)items.size();
	if (!buffer)
		return true;
	if (have < items.size())
		return false;
	std::copy(items.begin(), items.end(), buffer);
	return true;
}

void PostChangedEvents()
{
	// They go to the other programs too
	BaseSystem* system = GetUnsafeBaseSystem();
	if (!system)
		return;
	for (EVREventType type : { VREvent_ChaperoneDataHasChanged, VREvent_ChaperoneUniverseHasChanged }) {
		VREvent_t e{};
		e.eventType = type;
		e.trackedDeviceIndex = k_unTrackedDeviceIndexInvalid;
		system->_EnqueueEvent(e);
	}
}

} // namespace

ChaperoneData BaseChaperoneSetup::LiveData()
{
	if (auto committed = ChaperoneData::FromJson(SharedStore::Get().Read(StoreDoc::Chaperone)))
		return *committed;

	// The runtime's stage
	BackendManager* backends = BackendManager::InstancePtr();
	int count = 0;
	if (backends && backends->GetPlayAreaPoints(nullptr, &count) && count >= 2) {
		std::vector<HmdVector3_t> points(count);
		backends->GetPlayAreaPoints(points.data(), nullptr);
		float minX = points[0].v[0], maxX = minX, minZ = points[0].v[2], maxZ = minZ;
		for (const HmdVector3_t& p : points) {
			minX = std::min(minX, p.v[0]);
			maxX = std::max(maxX, p.v[0]);
			minZ = std::min(minZ, p.v[2]);
			maxZ = std::max(maxZ, p.v[2]);
		}
		return ChaperoneData::FromStage(maxX - minX, maxZ - minZ);
	}
	return ChaperoneData{};
}

ChaperoneData& BaseChaperoneSetup::Working()
{
	if (!working)
		working = LiveData();
	return *working;
}

bool BaseChaperoneSetup::CommitWorkingCopy(EChaperoneConfigFile configFile)
{
	std::lock_guard guard(lock);
	// The temporary config is a preview: nothing to keep
	if (configFile == EChaperoneConfigFile_Temp)
		return true;
	Json::Value data = Working().ToJson();
	if (!SharedStore::Get().Update(StoreDoc::Chaperone, [&](Json::Value& doc) { doc = data; }))
		return false;
	PostChangedEvents();
	return true;
}
void BaseChaperoneSetup::RevertWorkingCopy()
{
	std::lock_guard guard(lock);
	working = LiveData();
}
bool BaseChaperoneSetup::GetWorkingPlayAreaSize(float* pSizeX, float* pSizeZ)
{
	std::lock_guard guard(lock);
	if (!pSizeX || !pSizeZ)
		return false;
	*pSizeX = Working().playArea[0];
	*pSizeZ = Working().playArea[1];
	return *pSizeX > 0 && *pSizeZ > 0;
}
bool BaseChaperoneSetup::GetWorkingPlayAreaRect(HmdQuad_t* rect)
{
	std::lock_guard guard(lock);
	if (!rect)
		return false;
	const ChaperoneData& d = Working();
	*rect = ChaperoneData::FloorQuad(d.playArea[0], d.playArea[1]);
	return d.playArea[0] > 0 && d.playArea[1] > 0;
}
bool BaseChaperoneSetup::GetWorkingCollisionBoundsInfo(VR_OUT_ARRAY_COUNT(punQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t* punQuadsCount)
{
	std::lock_guard guard(lock);
	return CopyArray(Working().collisionBounds, pQuadsBuffer, punQuadsCount);
}
bool BaseChaperoneSetup::GetLiveCollisionBoundsInfo(VR_OUT_ARRAY_COUNT(punQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t* punQuadsCount)
{
	// The runtime's boundary, as a quad on the floor around the play area
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

	std::vector<HmdQuad_t> quads = { ChaperoneData::FloorQuad(bounds.width, bounds.height) };
	return CopyArray(quads, pQuadsBuffer, punQuadsCount);
}
bool BaseChaperoneSetup::GetWorkingSeatedZeroPoseToRawTrackingPose(HmdMatrix34_t* pmatSeatedZeroPoseToRawTrackingPose)
{
	std::lock_guard guard(lock);
	if (!pmatSeatedZeroPoseToRawTrackingPose)
		return false;
	*pmatSeatedZeroPoseToRawTrackingPose = Working().seatedZero;
	return true;
}
bool BaseChaperoneSetup::GetWorkingStandingZeroPoseToRawTrackingPose(HmdMatrix34_t* pmatStandingZeroPoseToRawTrackingPose)
{
	std::lock_guard guard(lock);
	if (!pmatStandingZeroPoseToRawTrackingPose)
		return false;
	*pmatStandingZeroPoseToRawTrackingPose = Working().standingZero;
	return true;
}
void BaseChaperoneSetup::SetWorkingPlayAreaSize(float sizeX, float sizeZ)
{
	// VRGIN (a VR mod framework) sets it to hide the chaperone during seated play: kept in the working copy only,
	// unless the program commits it
	std::lock_guard guard(lock);
	Working().playArea[0] = sizeX;
	Working().playArea[1] = sizeZ;
}
void BaseChaperoneSetup::SetWorkingCollisionBoundsInfo(VR_ARRAY_COUNT(unQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t unQuadsCount)
{
	std::lock_guard guard(lock);
	if (!pQuadsBuffer)
		unQuadsCount = 0;
	Working().collisionBounds.assign(pQuadsBuffer, pQuadsBuffer + unQuadsCount);
}
void BaseChaperoneSetup::SetWorkingSeatedZeroPoseToRawTrackingPose(const HmdMatrix34_t* pMatSeatedZeroPoseToRawTrackingPose)
{
	std::lock_guard guard(lock);
	if (pMatSeatedZeroPoseToRawTrackingPose)
		Working().seatedZero = *pMatSeatedZeroPoseToRawTrackingPose;
}
void BaseChaperoneSetup::SetWorkingStandingZeroPoseToRawTrackingPose(const HmdMatrix34_t* pMatStandingZeroPoseToRawTrackingPose)
{
	std::lock_guard guard(lock);
	if (pMatStandingZeroPoseToRawTrackingPose)
		Working().standingZero = *pMatStandingZeroPoseToRawTrackingPose;
}
void BaseChaperoneSetup::ReloadFromDisk(EChaperoneConfigFile configFile)
{
	std::lock_guard guard(lock);
	working = LiveData();
}
bool BaseChaperoneSetup::GetLiveSeatedZeroPoseToRawTrackingPose(HmdMatrix34_t* pmatSeatedZeroPoseToRawTrackingPose)
{
	if (!pmatSeatedZeroPoseToRawTrackingPose)
		return false;
	// A committed one; without, none
	if (auto committed = ChaperoneData::FromJson(SharedStore::Get().Read(StoreDoc::Chaperone))) {
		*pmatSeatedZeroPoseToRawTrackingPose = committed->seatedZero;
		return true;
	}
	*pmatSeatedZeroPoseToRawTrackingPose = {};
	return false;
}
void BaseChaperoneSetup::SetWorkingCollisionBoundsTagsInfo(VR_ARRAY_COUNT(unTagCount) uint8_t* pTagsBuffer, uint32_t unTagCount)
{
	std::lock_guard guard(lock);
	if (!pTagsBuffer)
		unTagCount = 0;
	Working().tags.assign(pTagsBuffer, pTagsBuffer + unTagCount);
}
bool BaseChaperoneSetup::GetLiveCollisionBoundsTagsInfo(VR_OUT_ARRAY_COUNT(punTagCount) uint8_t* pTagsBuffer, uint32_t* punTagCount)
{
	std::vector<uint8_t> tags;
	if (auto committed = ChaperoneData::FromJson(SharedStore::Get().Read(StoreDoc::Chaperone)))
		tags = committed->tags;
	if (tags.empty()) {
		if (punTagCount)
			*punTagCount = 0;
		return false;
	}
	return CopyArray(tags, pTagsBuffer, punTagCount);
}
bool BaseChaperoneSetup::SetWorkingPhysicalBoundsInfo(VR_ARRAY_COUNT(unQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t unQuadsCount)
{
	std::lock_guard guard(lock);
	if (!pQuadsBuffer)
		unQuadsCount = 0;
	Working().physicalBounds.assign(pQuadsBuffer, pQuadsBuffer + unQuadsCount);
	return true;
}
bool BaseChaperoneSetup::GetLivePhysicalBoundsInfo(VR_OUT_ARRAY_COUNT(punQuadsCount) HmdQuad_t* pQuadsBuffer, uint32_t* punQuadsCount)
{
	std::vector<HmdQuad_t> quads;
	if (auto committed = ChaperoneData::FromJson(SharedStore::Get().Read(StoreDoc::Chaperone)))
		quads = committed->physicalBounds;
	if (quads.empty()) {
		if (punQuadsCount)
			*punQuadsCount = 0;
		return false;
	}
	return CopyArray(quads, pQuadsBuffer, punQuadsCount);
}
bool BaseChaperoneSetup::ExportLiveToBuffer(VR_OUT_STRING() char* pBuffer, uint32_t* pnBufferLength)
{
	if (!pnBufferLength)
		return false;
	Json::StreamWriterBuilder builder;
	builder["indentation"] = "";
	std::string text = Json::writeString(builder, LiveData().ToJson());
	uint32_t needed = (uint32_t)text.size() + 1;
	uint32_t have = *pnBufferLength;
	*pnBufferLength = needed;
	if (!pBuffer || have < needed) {
		if (pBuffer && have)
			pBuffer[0] = 0;
		return false;
	}
	memcpy(pBuffer, text.c_str(), needed);
	return true;
}
bool BaseChaperoneSetup::ImportFromBufferToWorking(const char* pBuffer, uint32_t nImportFlags)
{
	if (!pBuffer)
		return false;
	Json::Value value;
	std::string error;
	if (!ParseJsonLenient(pBuffer, value, error))
		return false;
	std::optional<ChaperoneData> imported = ChaperoneData::FromJson(value);
	if (!imported)
		return false;
	std::lock_guard guard(lock);
	ChaperoneData& d = Working();
	if (nImportFlags & EChaperoneImport_BoundsOnly) {
		d.playArea[0] = imported->playArea[0];
		d.playArea[1] = imported->playArea[1];
		d.collisionBounds = imported->collisionBounds;
		d.tags = imported->tags;
	} else {
		d = *imported;
	}
	return true;
}
void BaseChaperoneSetup::SetWorkingPerimeter(VR_ARRAY_COUNT(unPointCount) const HmdVector2_t* pPointBuffer, uint32_t unPointCount)
{
	std::lock_guard guard(lock);
	if (!pPointBuffer)
		unPointCount = 0;
	Working().perimeter.assign(pPointBuffer, pPointBuffer + unPointCount);
}
void BaseChaperoneSetup::ShowWorkingSetPreview()
{
	// The OpenXR runtime draws its own boundary; there's no preview to show
}
void BaseChaperoneSetup::HideWorkingSetPreview()
{
	// No preview is shown
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
