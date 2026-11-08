#include "stdafx.h"
#define BASE_IMPL
#include "BaseDebug.h"

#include <cstring>

OOVR_EVRDebugError BaseDebug::EmitVrProfilerEvent(const char* pchMessage)
{
	return OOVR_VRDebugError_Success;
}

OOVR_EVRDebugError BaseDebug::BeginVrProfilerEvent(OOVR_VrProfilerEventHandle_t* pHandleOut)
{
	if (!pHandleOut)
		return OOVR_VRDebugError_BadParameter;
	*pHandleOut = nextEvent++;
	return OOVR_VRDebugError_Success;
}

OOVR_EVRDebugError BaseDebug::FinishVrProfilerEvent(OOVR_VrProfilerEventHandle_t hHandle, const char* pchMessage)
{
	return OOVR_VRDebugError_Success;
}

uint32_t BaseDebug::DriverDebugRequest(vr::TrackedDeviceIndex_t unDeviceIndex, const char* pchRequest, char* pchResponseBuffer,
    uint32_t unResponseBufferSize)
{
	// No driver answers: an empty response, whose size (1, with its terminator) SteamVR gives; 0 without a buffer
	if (!pchResponseBuffer || !unResponseBufferSize)
		return 0;
	memset(pchResponseBuffer, 0, unResponseBufferSize);
	return 1;
}
