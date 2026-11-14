#pragma once
#include "../BaseCommon.h"

// IVRIOBuffer_001 and 002 declare these in their own namespaces
typedef uint64_t OOVR_IOBufferHandle_t;
typedef int OOVR_EIOBufferMode;
enum OOVR_EIOBufferError {
	OOVR_IOBuffer_Success = 0,
	OOVR_IOBuffer_OperationFailed = 100,
	OOVR_IOBuffer_InvalidHandle = 101,
	OOVR_IOBuffer_InvalidArgument = 102,
	OOVR_IOBuffer_PathExists = 103,
	OOVR_IOBuffer_PathDoesNotExist = 104,
	OOVR_IOBuffer_Permission = 105,
};

/** SteamVR's shared-memory buffers between processes and drivers: none can be opened here. As SteamVR answers: a path it
 * can't open is InvalidArgument, and a handle that isn't open InvalidHandle. */
class BaseIOBuffer {
public:
	OOVR_EIOBufferError Open(const char* pchPath, OOVR_EIOBufferMode mode, uint32_t unElementSize, uint32_t unElements, OOVR_IOBufferHandle_t* pulBuffer);
	OOVR_EIOBufferError Close(OOVR_IOBufferHandle_t ulBuffer);
	OOVR_EIOBufferError Read(OOVR_IOBufferHandle_t ulBuffer, void* pDst, uint32_t unBytes, uint32_t* punRead);
	OOVR_EIOBufferError Write(OOVR_IOBufferHandle_t ulBuffer, void* pSrc, uint32_t unBytes);
	vr::PropertyContainerHandle_t PropertyContainer(OOVR_IOBufferHandle_t ulBuffer);
	bool HasReaders(OOVR_IOBufferHandle_t ulBuffer);
};
