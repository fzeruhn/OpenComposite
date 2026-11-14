#pragma once
#include "../BaseCommon.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

// IVRPaths_001/002 declare their path handle in their own namespaces
typedef uint64_t OOVR_PathHandle_t;

/**
 * SteamVR's path-keyed property store. The paths are kept (StringToHandle and HandleToString agree, the same path
 * always giving the same handle); there are no properties behind them, so every batch entry is UnknownProperty.
 */
class BasePaths {
public:
	vr::ETrackedPropertyError StringToHandle(OOVR_PathHandle_t* pHandle, const char* pchPath);
	vr::ETrackedPropertyError HandleToString(OOVR_PathHandle_t pHandle, char* pchBuffer, uint32_t unBufferSize, uint32_t* punBufferSizeUsed);

	// The batch structs differ between the versions (002's PathWrite_t has two more fields), so these take each one's
	template <typename PathRead>
	vr::ETrackedPropertyError ReadPathBatch(vr::PropertyContainerHandle_t ulRootHandle, PathRead* pBatch, uint32_t unBatchEntryCount)
	{
		if (unBatchEntryCount && !pBatch)
			return vr::TrackedProp_InvalidOperation;
		for (uint32_t i = 0; i < unBatchEntryCount; i++) {
			pBatch[i].unTag = vr::k_unInvalidPropertyTag;
			pBatch[i].unRequiredBufferSize = 0;
			pBatch[i].eError = vr::TrackedProp_UnknownProperty;
		}
		return vr::TrackedProp_Success;
	}

	template <typename PathWrite>
	vr::ETrackedPropertyError WritePathBatch(vr::PropertyContainerHandle_t ulRootHandle, PathWrite* pBatch, uint32_t unBatchEntryCount)
	{
		if (unBatchEntryCount && !pBatch)
			return vr::TrackedProp_InvalidOperation;
		for (uint32_t i = 0; i < unBatchEntryCount; i++)
			pBatch[i].eError = vr::TrackedProp_UnknownProperty;
		return vr::TrackedProp_Success;
	}

private:
	std::mutex lock;
	std::map<std::string, OOVR_PathHandle_t> handles;
	std::vector<std::string> paths; // handle - 1
};
