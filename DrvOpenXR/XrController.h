#pragma once

#include "Misc/Input/InteractionProfile.h"
#include "Misc/LibMonado.h"
#include "XrTrackedDevice.h"

#include <optional>

class BaseInput;

class XrController : public XrTrackedDevice {
public:
	enum XrControllerType : int {
		XCT_LEFT,
		XCT_RIGHT,
		XCT_TRACKED_OBJECT,
	};

	explicit XrController(XrControllerType type, const InteractionProfile& profile);

	TrackedDeviceType GetHand() override;

	void GetPose(vr::ETrackingUniverseOrigin origin, vr::TrackedDevicePose_t* pose, ETrackingStateType trackingState) override;
	void GetPoseFromHandTracking(BaseInput* input, vr::TrackedDevicePose_t* pose);

	vr::ETrackedDeviceClass GetTrackedDeviceClass() override;

	const InteractionProfile* GetInteractionProfile() override;

	// properties
	bool GetBoolTrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL) override;
	int32_t GetInt32TrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL) override;
	float GetFloatTrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL) override;
	uint64_t GetUint64TrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL) override;
	uint32_t GetStringTrackedDeviceProperty(vr::ETrackedDeviceProperty prop, char* pchValue,
	    uint32_t unBufferSize, vr::ETrackedPropertyError* pErrorL) override;

	DeviceIdentity Identity() override;

	/** A bool property from the battery libmonado reports for a role ("left", "right"; null for none): the answer, or
	 * nothing for the other properties, which ask libmonado nothing */
	static std::optional<bool> BatteryBoolProperty(LibMonado& mnd, vr::ETrackedDeviceProperty prop, const char* role)
	{
		switch (prop) {
		case vr::Prop_DeviceProvidesBatteryStatus_Bool: {
			std::optional<LibMonado::Battery> battery = role ? mnd.BatteryForRole(role) : std::nullopt;
			return battery ? battery->present : true;
		}
		case vr::Prop_DeviceIsCharging_Bool: {
			std::optional<LibMonado::Battery> battery = role ? mnd.BatteryForRole(role) : std::nullopt;
			if (battery && battery->present)
				return battery->charging;
			return std::nullopt;
		}
		default:
			return std::nullopt;
		}
	}

	bool IsPoseFromHandTracking() override;
	bool IsHandTrackingValid() override;
	void SetHandTrackingValid(bool valid) override;

private:
	XrControllerType type;
	const InteractionProfile& profile;
	bool isHandTrackingValid = false;
	bool isPoseFromHandTracking = false;
};
