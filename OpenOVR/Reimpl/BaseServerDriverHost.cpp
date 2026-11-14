#include "stdafx.h"
#define BASE_IMPL
#include "BaseServerDriverHost.h"

#include "Drivers/DriverManager.h"

bool BaseServerDriverHost::TrackedDeviceAdded(OCTrackedDeviceDriver* driver)
{
	return DriverManager::CheckInstance().TrackedDeviceAdded(driver);
}
void BaseServerDriverHost::TrackedDevicePoseUpdated(uint32_t unWhichDevice, const OCDriverPose_t& newPose, uint32_t unPoseStructSize)
{
	// OpenComposite's own drivers get their poses from OpenXR, not from this
}
void BaseServerDriverHost::VsyncEvent(double vsyncTimeOffsetSeconds)
{
	// Timing comes from the OpenXR runtime
}
void BaseServerDriverHost::VendorSpecificEvent(uint32_t unWhichDevice, vr::EVREventType eventType, const vr::VREvent_Data_t& eventData, double eventTimeOffset)
{
	// No drivers listen for vendor events
}
bool BaseServerDriverHost::IsExiting()
{
	return false;
}
bool BaseServerDriverHost::PollNextEvent(vr::VREvent_t* pEvent, uint32_t uncbVREvent)
{
	// No events for drivers
	if (pEvent && uncbVREvent)
		memset(pEvent, 0, uncbVREvent);
	return false;
}
void BaseServerDriverHost::GetRawTrackedDevicePoses(float fPredictedSecondsFromNow, vr::TrackedDevicePose_t* pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount)
{
	// No raw poses for drivers: every pose is invalid
	if (pTrackedDevicePoseArray)
		memset(pTrackedDevicePoseArray, 0, sizeof(vr::TrackedDevicePose_t) * unTrackedDevicePoseArrayCount);
}
void BaseServerDriverHost::TrackedDeviceDisplayTransformUpdated(uint32_t unWhichDevice, vr::HmdMatrix34_t eyeToHeadLeft, vr::HmdMatrix34_t eyeToHeadRight)
{
	// The eye transforms come from the OpenXR runtime
}
