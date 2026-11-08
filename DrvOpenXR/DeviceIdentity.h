#pragma once

// What each tracked device says it is: the property answers that name it, from one table instead of fixed values
// spread over the device classes. An HMD takes the row of its OpenXR system's name, else the default row.

#include "../OpenOVR/Misc/Input/InteractionProfile.h"

#include <cstdint>
#include <string>

struct DeviceIdentity {
	// An empty string isn't answered by the table: the interaction profile's property, or the generic answer, is
	std::string trackingSystem, manufacturer, model, modelNumber, renderModel, controllerType, registeredDeviceType,
	    inputProfilePath, serial;
	uint64_t supportedButtons = 0;
	vr::ETrackedControllerRole role = vr::TrackedControllerRole_Invalid;
};

/** The row for the OpenXR system of that name, else the default row (an Oculus HMD's answers) */
const DeviceIdentity& HmdIdentity(const std::string& xrSystemName);

/** A controller of that profile in that hand: the profile's OpenVR name and render models, its buttons */
DeviceIdentity ControllerIdentity(const InteractionProfile& profile, vr::ETrackedControllerRole role);

/** A tracker (XR_MNDX_xdev_space): the generic tracker answers, with its serial */
DeviceIdentity TrackerIdentity(const std::string& xdevName, const std::string& serial);

/** EVRButtonId bits from the profile's valid input paths, for one hand (or both, for TrackedControllerRole_Invalid).
 * The system button is always there: the OpenXR runtime keeps it, as SteamVR's drivers report it. */
uint64_t SupportedButtonsFor(const InteractionProfile& profile, vr::ETrackedControllerRole role = vr::TrackedControllerRole_Invalid);
