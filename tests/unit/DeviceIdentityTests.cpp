// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "../DrvOpenXR/DeviceIdentity.h"
#include "Misc/Input/IndexControllerInteractionProfile.h"
#include "Misc/Input/KhrSimpleInteractionProfile.h"
#include "Misc/Input/OculusInteractionProfile.h"

TEST_CASE("Hmd_default_row_matches_todays_answers")
{
	// What every HMD answered before the table (XrTrackedDevice.cpp and XrHMD.cpp's fixed values)
	const DeviceIdentity& hmd = HmdIdentity("");
	CHECK(hmd.trackingSystem == "oculus");
	CHECK(hmd.manufacturer == "Oculus");
	CHECK(hmd.renderModel == "oculusHmdRenderModel");
	CHECK(hmd.controllerType == "oculus"); // VRChat ignores an HMD without one
	CHECK(hmd.registeredDeviceType == "oculus/F00BAAF00F");
	CHECK(hmd.serial == "<unknown>");
	CHECK(hmd.model.empty()); // none answered: the interaction profile's, if any
	CHECK(hmd.modelNumber.empty());
	CHECK(hmd.inputProfilePath.empty());
	CHECK(hmd.supportedButtons == 0);
	CHECK(hmd.role == vr::TrackedControllerRole_Invalid);
}

TEST_CASE("Unknown_system_gets_default_row")
{
	CHECK(&HmdIdentity("A runtime nobody has heard of") == &HmdIdentity(""));
	CHECK(HmdIdentity("Monado: Simulated HMD").trackingSystem == "oculus");
}

TEST_CASE("Index_controller_identity")
{
	IndexControllerInteractionProfile index;
	DeviceIdentity right = ControllerIdentity(index, vr::TrackedControllerRole_RightHand);
	CHECK(right.controllerType == "knuckles");
	CHECK(right.renderModel == "{indexcontroller}valve_controller_knu_1_0_right");
	CHECK(right.registeredDeviceType == "oculus/F00BAAF00F_Controller_Right");
	CHECK(right.role == vr::TrackedControllerRole_RightHand);
	CHECK((right.supportedButtons & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0);
	CHECK((right.supportedButtons & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) != 0); // the trackpad and stick are Axis0

	DeviceIdentity left = ControllerIdentity(index, vr::TrackedControllerRole_LeftHand);
	CHECK(left.renderModel == "{indexcontroller}valve_controller_knu_1_0_left");
	CHECK(left.registeredDeviceType == "oculus/F00BAAF00F_Controller_Left");
}

TEST_CASE("Touch_buttons_include_A_and_B_on_right")
{
	OculusTouchInteractionProfile touch;
	uint64_t right = ControllerIdentity(touch, vr::TrackedControllerRole_RightHand).supportedButtons;
	CHECK((right & vr::ButtonMaskFromId(vr::k_EButton_A)) != 0);
	CHECK((right & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) != 0); // B, as SteamVR's Touch driver has it
	CHECK((right & vr::ButtonMaskFromId(vr::k_EButton_Grip)) != 0);
	CHECK((right & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0);

	// The left hand has X and Y (A and ApplicationMenu too) and the menu button
	uint64_t left = ControllerIdentity(touch, vr::TrackedControllerRole_LeftHand).supportedButtons;
	CHECK((left & vr::ButtonMaskFromId(vr::k_EButton_A)) != 0);
	CHECK((left & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) != 0);
	CHECK(ControllerIdentity(touch, vr::TrackedControllerRole_LeftHand).controllerType == "oculus_touch");
}

TEST_CASE("Simple_controller_select_is_the_trigger")
{
	KhrSimpleInteractionProfile simple;
	uint64_t right = ControllerIdentity(simple, vr::TrackedControllerRole_RightHand).supportedButtons;
	CHECK((right & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0);
	CHECK((right & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) != 0);
}
