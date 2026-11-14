#include "XrController.h"

// HACK: grab the pose from BaseInput
#include "../OpenOVR/Misc/xrmoreutils.h"
#include "../OpenOVR/Reimpl/BaseInput.h"
#include "generated/static_bases.gen.h"

XrController::XrController(XrController::XrControllerType type, const InteractionProfile& profile)
    : type(type), profile(profile)
{
	InitialiseDevice(GetHand() + 1);
}

#define TRY_PROFILE_PROP(type)                                                \
	do {                                                                      \
		std::optional<type> ret = profile.GetProperty<type>(prop, GetHand()); \
		if (ret.has_value())                                                  \
			return *ret;                                                      \
	} while (0)

// properties
bool XrController::GetBoolTrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL)
{
	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

	TRY_PROFILE_PROP(bool);

	switch (prop) {
	case vr::Prop_DeviceProvidesBatteryStatus_Bool:
		return true;
	default:
		return XrTrackedDevice::GetBoolTrackedDeviceProperty(prop, pErrorL);
	}
}

int32_t XrController::GetInt32TrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL)
{
	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

	TRY_PROFILE_PROP(int32_t);

	// Continue to pretend to be a CV1
	// Don't apply the inputs to the tracking object
	if (type == XCT_LEFT || type == XCT_RIGHT) {
		switch (prop) {
		case vr::Prop_Axis0Type_Int32:
			// TODO find out which of these SteamVR returns and do likewise
			// return vr::k_eControllerAxis_TrackPad;
			return vr::k_eControllerAxis_Joystick;

		case vr::Prop_Axis1Type_Int32:
		case vr::Prop_Axis2Type_Int32:
			return vr::k_eControllerAxis_Trigger;

		case vr::Prop_Axis3Type_Int32:
		case vr::Prop_Axis4Type_Int32:
			return vr::k_eControllerAxis_None;

		default:
			break;
		}
	}

	return XrTrackedDevice::GetInt32TrackedDeviceProperty(prop, pErrorL);
}

uint64_t XrController::GetUint64TrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL)
{
	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

	TRY_PROFILE_PROP(uint64_t);

	// The old input system's buttons: those the controller's interaction profile has (DeviceIdentity)
	if (prop == vr::Prop_SupportedButtons_Uint64)
		return Identity().supportedButtons;

	return XrTrackedDevice::GetUint64TrackedDeviceProperty(prop, pErrorL);
}

uint32_t XrController::GetStringTrackedDeviceProperty(vr::ETrackedDeviceProperty prop,
    char* value, uint32_t bufferSize, vr::ETrackedPropertyError* pErrorL)
{

	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

	std::optional<std::string> ret = profile.GetProperty<std::string>(prop, GetHand());
	if (ret.has_value())
		return AnswerString(*ret, value, bufferSize, pErrorL);

	// The rest from the controller's row (Identity): Resonite tells controllers apart by the render model name
	return XrTrackedDevice::GetStringTrackedDeviceProperty(prop, value, bufferSize, pErrorL);
}

DeviceIdentity XrController::Identity()
{
	switch (type) {
	case XCT_LEFT:
		return ControllerIdentity(*GetInteractionProfile(), vr::TrackedControllerRole_LeftHand);
	case XCT_RIGHT:
		return ControllerIdentity(*GetInteractionProfile(), vr::TrackedControllerRole_RightHand);
	default:
		// A tracked object (or a type OpenComposite doesn't know): no hand
		return ControllerIdentity(*GetInteractionProfile(), vr::TrackedControllerRole_Invalid);
	}
}

ITrackedDevice::TrackedDeviceType XrController::GetHand()
{
	switch (type) {
	case XCT_LEFT:
		return HAND_LEFT;
	case XCT_RIGHT:
		return HAND_RIGHT;
	default:
		return HAND_NONE;
	}
}

void XrController::GetPose(vr::ETrackingUniverseOrigin origin, vr::TrackedDevicePose_t* pose, ETrackingStateType trackingState)
{
	// Default to an invalid pose
	ZeroMemory(pose, sizeof(*pose));
	pose->bDeviceIsConnected = true;
	pose->bPoseIsValid = false;
	pose->eTrackingResult = vr::TrackingResult_Running_OutOfRange;

	BaseInput* input = GetUnsafeBaseInput();
	if (input == nullptr)
		return;

	// Lie and say the pose is valid if the actions haven't even been loaded yet.
	// This is a workaround for games like DCS, which appear to require valid poses before
	// it will even attempt to request the controller state (and thus create the actions).
	if (!input->AreActionsLoaded()) {
		pose->bPoseIsValid = true;
		pose->eTrackingResult = vr::TrackingResult_Running_OK;
	}

	// TODO do something with TrackingState
	XrSpace space = XR_NULL_HANDLE;

	// Specifically use grip pose, since that's what InteractionProfile::GetGripToSteamVRTransform uses
	GetBaseInput()->GetHandSpace(DeviceIndex(), space, false);

	if (!space)
		return;

	// Find the hand transform matrix, and include that
	glm::mat4 transform = profile.GetGripToSteamVRTransform(GetHand());

	if (xr_utils::PoseFromSpace(pose, space, origin, transform)) {
		isPoseFromHandTracking = false;
		return;
	}

	// Fallback to pose from hand tracking if the controller one isn't valid
	GetPoseFromHandTracking(input, pose);
	isPoseFromHandTracking = true;
}

void XrController::GetPoseFromHandTracking(BaseInput* input, vr::TrackedDevicePose_t* pose)
{
	if (!xr_gbl || !xr_gbl->handTrackingProperties.supportsHandTracking)
		return;

	if (!input->handTrackers[(int)GetHand()])
		return;

	XrHandJointsLocateInfoEXT locateInfo = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
	locateInfo.baseSpace = xr_gbl->floorSpace;
	locateInfo.time = xr_gbl->GetBestTime();

	XrHandJointVelocitiesEXT velocities = { XR_TYPE_HAND_JOINT_VELOCITIES_EXT };
	velocities.jointCount = XR_HAND_JOINT_COUNT_EXT;
	std::vector<XrHandJointVelocityEXT> jointVelocities(velocities.jointCount);
	velocities.jointVelocities = jointVelocities.data();

	XrHandJointLocationsEXT locations = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
	locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
	std::vector<XrHandJointLocationEXT> jointLocations(locations.jointCount);
	locations.jointLocations = jointLocations.data();
	locations.next = &velocities;

	// On failure the pose stays as it was (the controller's), so the hand doesn't jump
	if (XR_FAILED(xr_ext->xrLocateHandJointsEXT(input->handTrackers[(int)GetHand()], &locateInfo, &locations)))
		return;

	xr_utils::PoseFromHandTracking(pose, locations, velocities, GetHand() == HAND_RIGHT);
}

vr::ETrackedDeviceClass XrController::GetTrackedDeviceClass()
{
	return vr::TrackedDeviceClass_Controller;
}

bool XrController::IsPoseFromHandTracking()
{
	return isPoseFromHandTracking;
}

bool XrController::IsHandTrackingValid()
{
	return isHandTrackingValid;
}

void XrController::SetHandTrackingValid(bool valid)
{
	isHandTrackingValid = valid;
}

const InteractionProfile* XrController::GetInteractionProfile()
{
	return &profile;
}
