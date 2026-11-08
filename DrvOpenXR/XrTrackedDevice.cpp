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

	// The device's row in the identity table; an empty field isn't answered here
	DeviceIdentity id = Identity();
	const std::string* field = nullptr;
	switch (prop) {
	case vr::Prop_TrackingSystemName_String:
		field = &id.trackingSystem;
		break;
	case vr::Prop_ManufacturerName_String:
		field = &id.manufacturer;
		break;
	case vr::Prop_ModelNumber_String:
		field = &id.modelNumber;
		break;
	case vr::Prop_RenderModelName_String:
		field = &id.renderModel; // passed on into IVRRenderModels as the render model name
		break;
	case vr::Prop_ControllerType_String:
		field = &id.controllerType;
		break;
	case vr::Prop_RegisteredDeviceType_String:
		field = &id.registeredDeviceType;
		break;
	case vr::Prop_InputProfilePath_String:
		field = &id.inputProfilePath;
		break;
	case vr::Prop_SerialNumber_String:
		field = &id.serial;
		break;
	default:
		break;
	}
	if (field && !field->empty())
		return AnswerString(*field, value, bufferSize, pErrorL);

	// Used by Firebird The Unfinished - see #58. Copied from SteamVR
	if (prop == vr::Prop_DriverVersion_String)
		return AnswerString("1.32.0", value, bufferSize, pErrorL);

	return ITrackedDevice::GetStringTrackedDeviceProperty(prop, value, bufferSize, pErrorL);
}

DeviceIdentity XrTrackedDevice::Identity()
{
	// A device of no class below: the generic answers
	DeviceIdentity id;
	id.trackingSystem = "oculus";
	id.manufacturer = "Oculus";
	id.serial = "<unknown>";
	id.renderModel = "<unknown>";
	return id;
}

uint32_t XrTrackedDevice::AnswerString(const std::string& text, char* value, uint32_t bufferSize, vr::ETrackedPropertyError* pErrorL)
{
	uint32_t needed = (uint32_t)text.size() + 1;
	if (value && bufferSize >= needed) {
		memcpy(value, text.c_str(), needed);
	} else {
		if (value && bufferSize)
			value[0] = '\0';
		if (pErrorL)
			*pErrorL = vr::TrackedProp_BufferTooSmall;
	}
	return needed;
}
