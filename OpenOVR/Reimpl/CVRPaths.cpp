#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("Paths", "001", CUSTOM)
GEN_INTERFACE("Paths", "002", CUSTOM)

#include "generated/GVRPaths.gen.h"

// Each version's own batch structs (BasePaths takes them as they are)
vr::ETrackedPropertyError CVRPaths_001::ReadPathBatch(vr::PropertyContainerHandle_t ulRootHandle, vr::IVRPaths_001::PathRead_t* pBatch,
    uint32_t unBatchEntryCount)
{
	return base->ReadPathBatch(ulRootHandle, pBatch, unBatchEntryCount);
}

vr::ETrackedPropertyError CVRPaths_001::WritePathBatch(vr::PropertyContainerHandle_t ulRootHandle, vr::IVRPaths_001::PathWrite_t* pBatch,
    uint32_t unBatchEntryCount)
{
	return base->WritePathBatch(ulRootHandle, pBatch, unBatchEntryCount);
}

vr::ETrackedPropertyError CVRPaths_002::ReadPathBatch(vr::PropertyContainerHandle_t ulRootHandle, vr::IVRPaths_002::PathRead_t* pBatch,
    uint32_t unBatchEntryCount)
{
	return base->ReadPathBatch(ulRootHandle, pBatch, unBatchEntryCount);
}

vr::ETrackedPropertyError CVRPaths_002::WritePathBatch(vr::PropertyContainerHandle_t ulRootHandle, vr::IVRPaths_002::PathWrite_t* pBatch,
    uint32_t unBatchEntryCount)
{
	return base->WritePathBatch(ulRootHandle, pBatch, unBatchEntryCount);
}
