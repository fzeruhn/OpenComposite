#include "stdafx.h"

#include "DeviceIdentity.h"

#include <map>

namespace {

DeviceIdentity DefaultHmd()
{
	// An Oculus HMD's answers, as SteamVR's Oculus driver gives them
	DeviceIdentity hmd;
	hmd.trackingSystem = "oculus";
	hmd.manufacturer = "Oculus";
	hmd.renderModel = "oculusHmdRenderModel";
	hmd.controllerType = "oculus"; // VRChat ignores an HMD without one
	hmd.registeredDeviceType = "oculus/F00BAAF00F";
	hmd.serial = "<unknown>";
	return hmd;
}

// The rows by OpenXR system name. None: every HMD takes the default row.
const std::map<std::string, DeviceIdentity>& HmdRows()
{
	static const std::map<std::string, DeviceIdentity> rows = {};
	return rows;
}

// The button an input path's component gives (the part after /input/), as SteamVR's drivers map them
uint64_t ButtonsForComponent(const std::string& component)
{
	using vr::ButtonMaskFromId;
	if (component == "system")
		return ButtonMaskFromId(vr::k_EButton_System);
	if (component == "menu" || component == "b" || component == "y")
		return ButtonMaskFromId(vr::k_EButton_ApplicationMenu); // the Touch driver's B and Y
	if (component == "a" || component == "x")
		return ButtonMaskFromId(vr::k_EButton_A);
	if (component == "squeeze")
		return ButtonMaskFromId(vr::k_EButton_Grip) | ButtonMaskFromId(vr::k_EButton_Axis2);
	if (component == "trigger" || component == "select") // the simple controller's select is its trigger
		return ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger);
	if (component == "thumbstick" || component == "trackpad") {
		// Axis0, and the d-pad the legacy input reads from it
		return ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad) | ButtonMaskFromId(vr::k_EButton_DPad_Left)
		    | ButtonMaskFromId(vr::k_EButton_DPad_Up) | ButtonMaskFromId(vr::k_EButton_DPad_Right)
		    | ButtonMaskFromId(vr::k_EButton_DPad_Down);
	}
	return 0;
}

} // namespace

const DeviceIdentity& HmdIdentity(const std::string& xrSystemName)
{
	static const DeviceIdentity defaultRow = DefaultHmd();
	auto it = HmdRows().find(xrSystemName);
	return it == HmdRows().end() ? defaultRow : it->second;
}

uint64_t SupportedButtonsFor(const InteractionProfile& profile, vr::ETrackedControllerRole role)
{
	uint64_t buttons = vr::ButtonMaskFromId(vr::k_EButton_System);
	std::string hand = role == vr::TrackedControllerRole_LeftHand ? "/user/hand/left/input/"
	    : role == vr::TrackedControllerRole_RightHand            ? "/user/hand/right/input/"
	                                                             : "";
	for (const std::string& path : profile.GetValidInputPaths()) {
		size_t input = path.find("/input/");
		if (input == std::string::npos || (!hand.empty() && path.compare(0, hand.size(), hand) != 0))
			continue;
		size_t start = input + 7, end = path.find('/', start);
		buttons |= ButtonsForComponent(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
	}
	return buttons;
}

DeviceIdentity ControllerIdentity(const InteractionProfile& profile, vr::ETrackedControllerRole role)
{
	DeviceIdentity id;
	// The generic device answers (XrTrackedDevice::Identity's)
	id.trackingSystem = "oculus";
	id.manufacturer = "Oculus";
	id.serial = "<unknown>";
	id.role = role;
	if (std::optional<const char*> name = profile.GetOpenVRName())
		id.controllerType = *name;

	// Resonite tells controllers apart by their render model: the profile's, else OpenComposite's own
	bool left = role == vr::TrackedControllerRole_LeftHand;
	if (role == vr::TrackedControllerRole_LeftHand || role == vr::TrackedControllerRole_RightHand) {
		std::optional<const char*> model = left ? profile.GetLeftHandRenderModelName() : profile.GetRightHandRenderModelName();
		id.renderModel = model ? *model : (left ? "renderLeftHand" : "renderRightHand");
		id.registeredDeviceType = left ? "oculus/F00BAAF00F_Controller_Left" : "oculus/F00BAAF00F_Controller_Right";
	} else {
		id.renderModel = "renderObject0";
	}
	id.supportedButtons = SupportedButtonsFor(profile, role);
	return id;
}

DeviceIdentity TrackerIdentity(const std::string& xdevName, const std::string& serial)
{
	// The generic tracker answers (XrGenericTracker.cpp); the render model is the profile's OpenVR name, set by the tracker
	DeviceIdentity id;
	id.trackingSystem = "oculus";
	id.manufacturer = "Oculus";
	id.controllerType = "vive_tracker_handheld_object";
	id.modelNumber = "Vive Tracker Handheld Object";
	id.serial = serial;
	id.role = vr::TrackedControllerRole_Invalid;
	return id;
}
