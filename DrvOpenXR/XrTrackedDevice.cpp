//
// Created by ZNix on 25/10/2020.
//

#include "XrTrackedDevice.h"

#include "../OpenOVR/convert.h"

// A device type that doesn't track itself: not connected, no pose
static void NoPose(vr::TrackedDevicePose_t* pose)
{
	if (!pose)
		return;
	*pose = {};
	pose->eTrackingResult = vr::TrackingResult_Uninitialized;
	pose->bPoseIsValid = false;
	pose->bDeviceIsConnected = false;
}

void XrTrackedDevice::GetPose(vr::ETrackingUniverseOrigin origin, vr::TrackedDevicePose_t* pose, ETrackingStateType trackingState)
{
	NoPose(pose);
}

void XrTrackedDevice::GetPose(vr::ETrackingUniverseOrigin origin, vr::TrackedDevicePose_t* pose, ETrackingStateType trackingState, double absTime)
{
	NoPose(pose);
}

// Properties

uint64_t XrTrackedDevice::GetUint64TrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL)
{
	if (prop == vr::Prop_CurrentUniverseId_Uint64) {
		if (pErrorL)
			*pErrorL = vr::TrackedProp_Success;

		return 1; // Oculus Rift's universe
	}

	return ITrackedDevice::GetUint64TrackedDeviceProperty(prop, pErrorL);
}

uint32_t XrTrackedDevice::GetStringTrackedDeviceProperty(vr::ETrackedDeviceProperty prop,
    char* value, uint32_t bufferSize, vr::ETrackedPropertyError* pErrorL)
{

	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

#define PROP(in, out)                                                                                  \
	if (prop == in) {                                                                                  \
		if (value != NULL && bufferSize > 0) {                                                         \
			strcpy_s(value, bufferSize, out); /* FFS msvc - strncpy IS the secure version of strcpy */ \
		}                                                                                              \
		return (uint32_t)strlen(out) + 1;                                                              \
	}

	// These have been validated against SteamVR
	PROP(vr::Prop_TrackingSystemName_String, "oculus");
	PROP(vr::Prop_ManufacturerName_String, "Oculus");

	// TODO these?
	PROP(vr::Prop_SerialNumber_String, "<unknown>"); // TODO
	PROP(vr::Prop_RenderModelName_String, "<unknown>"); // It appears this just gets passed into IVRRenderModels as the render model name

	// Used by Firebird The Unfinished - see #58
	// Copied from SteamVR
	PROP(vr::Prop_DriverVersion_String, "1.32.0");

#undef PROP

	return ITrackedDevice::GetStringTrackedDeviceProperty(prop, value, bufferSize, pErrorL);
}
