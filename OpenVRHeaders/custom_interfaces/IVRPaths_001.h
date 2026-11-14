#pragma once
#include "generated/interfaces/openvr.h"
#include "generated/interfaces/vrtypes.h"
#include "generated/interfaces/vrannotation.h"

// IVRPaths (SDK 1.12.5 to 2.12.14): SteamVR's path-keyed property store, offered by Valve only through the C API
// (FnTable:IVRPaths_001). Declared from Valve's headers/openvr_capi.h, whose VR_IVRPaths_FnTable has the methods in this
// order, and headers/openvr_api.json for their C++ types.

namespace vr
{
namespace IVRPaths_001
{

typedef uint64_t PathHandle_t;
static const PathHandle_t k_ulInvalidPathHandle = 0;

enum EPropertyWriteType
{
	PropertyWrite_Set = 0,
	PropertyWrite_Erase = 1,
	PropertyWrite_SetError = 2,
};

struct PathWrite_t
{
	PathHandle_t ulPath;
	EPropertyWriteType writeType;
	ETrackedPropertyError eSetError;
	void *pvBuffer;
	uint32_t unBufferSize;
	PropertyTypeTag_t unTag;
	ETrackedPropertyError eError;
	const char *pszPath;
};

struct PathRead_t
{
	PathHandle_t ulPath;
	void *pvBuffer;
	uint32_t unBufferSize;
	PropertyTypeTag_t unTag;
	uint32_t unRequiredBufferSize;
	ETrackedPropertyError eError;
	const char *pszPath;
};

class IVRPaths
{
public:
	virtual ETrackedPropertyError ReadPathBatch( PropertyContainerHandle_t ulRootHandle, PathRead_t *pBatch, uint32_t unBatchEntryCount ) = 0;
	virtual ETrackedPropertyError WritePathBatch( PropertyContainerHandle_t ulRootHandle, PathWrite_t *pBatch, uint32_t unBatchEntryCount ) = 0;
	virtual ETrackedPropertyError StringToHandle( PathHandle_t *pHandle, const char *pchPath ) = 0;
	virtual ETrackedPropertyError HandleToString( PathHandle_t pHandle, char *pchBuffer, uint32_t unBufferSize, uint32_t *punBufferSizeUsed ) = 0;
};

static const char * const IVRPaths_Version = "IVRPaths_001";

} // namespace IVRPaths_001

} // namespace vr
