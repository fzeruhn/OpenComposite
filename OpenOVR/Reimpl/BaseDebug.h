#pragma once
#include "../BaseCommon.h"

#include <atomic>

// IVRDebug_001 declares these in its own namespace
typedef uint64_t OOVR_VrProfilerEventHandle_t;
enum OOVR_EVRDebugError {
	OOVR_VRDebugError_Success = 0,
	OOVR_VRDebugError_BadParameter = 1,
};

/** SteamVR's profiler (its timing view): events are taken and dropped. There are no drivers to send requests to: their
 * response is empty. */
class BaseDebug {
public:
	OOVR_EVRDebugError EmitVrProfilerEvent(const char* pchMessage);
	OOVR_EVRDebugError BeginVrProfilerEvent(OOVR_VrProfilerEventHandle_t* pHandleOut);
	OOVR_EVRDebugError FinishVrProfilerEvent(OOVR_VrProfilerEventHandle_t hHandle, const char* pchMessage);
	uint32_t DriverDebugRequest(vr::TrackedDeviceIndex_t unDeviceIndex, const char* pchRequest, VR_OUT_STRING() char* pchResponseBuffer,
	    uint32_t unResponseBufferSize);

private:
	std::atomic<uint64_t> nextEvent{ 1 };
};
