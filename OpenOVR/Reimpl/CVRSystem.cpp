#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("System", "003")
GEN_INTERFACE("System", "004")
GEN_INTERFACE("System", "005")
GEN_INTERFACE("System", "006")
GEN_INTERFACE("System", "007")
GEN_INTERFACE("System", "009")
GEN_INTERFACE("System", "010")
GEN_INTERFACE("System", "011")
GEN_INTERFACE("System", "012")
//
GEN_INTERFACE("System", "014")
GEN_INTERFACE("System", "015")
GEN_INTERFACE("System", "016")
GEN_INTERFACE("System", "017")
// AFAIK 18 doesn't exist
GEN_INTERFACE("System", "019")
GEN_INTERFACE("System", "020")
GEN_INTERFACE("System", "021")
GEN_INTERFACE("System", "022")
GEN_INTERFACE("System", "023")
GEN_INTERFACE("System", "026")

#include "generated/GVRSystem.gen.h"

// Old methods that don't take size arguments. Up to IVRSystem_010 (SDK 0.9.14) the event is the old 32-byte one
// (PollNextEventOld); the controller state is the same as VRControllerState001_t throughout
bool CVRSystem_003::PollNextEvent(vr::VREvent_t* pEvent)
{
	return base->PollNextEventOld(vr::TrackingUniverseStanding, pEvent, nullptr);
}

bool CVRSystem_003::PollNextEventWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->PollNextEventOld(eOrigin, pEvent, pTrackedDevicePose);
}

bool CVRSystem_003::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_003::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}

bool CVRSystem_004::PollNextEvent(vr::VREvent_t* pEvent)
{
	return base->PollNextEventOld(vr::TrackingUniverseStanding, pEvent, nullptr);
}

bool CVRSystem_004::PollNextEventWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->PollNextEventOld(eOrigin, pEvent, pTrackedDevicePose);
}

bool CVRSystem_004::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_004::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}

bool CVRSystem_005::PollNextEvent(vr::VREvent_t* pEvent)
{
	return base->PollNextEventOld(vr::TrackingUniverseStanding, pEvent, nullptr);
}

bool CVRSystem_005::PollNextEventWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->PollNextEventOld(eOrigin, pEvent, pTrackedDevicePose);
}

bool CVRSystem_005::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_005::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}

bool CVRSystem_006::PollNextEvent(vr::VREvent_t* pEvent)
{
	return base->PollNextEventOld(vr::TrackingUniverseStanding, pEvent, nullptr);
}

bool CVRSystem_006::PollNextEventWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->PollNextEventOld(eOrigin, pEvent, pTrackedDevicePose);
}

bool CVRSystem_006::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_006::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}

bool CVRSystem_007::PollNextEvent(vr::VREvent_t* pEvent)
{
	return base->PollNextEventOld(vr::TrackingUniverseStanding, pEvent, nullptr);
}

bool CVRSystem_007::PollNextEventWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->PollNextEventOld(eOrigin, pEvent, pTrackedDevicePose);
}

bool CVRSystem_007::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_007::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}

bool CVRSystem_009::PollNextEvent(vr::VREvent_t* pEvent)
{
	return base->PollNextEventOld(vr::TrackingUniverseStanding, pEvent, nullptr);
}

bool CVRSystem_009::PollNextEventWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->PollNextEventOld(eOrigin, pEvent, pTrackedDevicePose);
}

bool CVRSystem_009::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_009::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}

bool CVRSystem_010::PollNextEvent(vr::VREvent_t* pEvent)
{
	return base->PollNextEventOld(vr::TrackingUniverseStanding, pEvent, nullptr);
}

bool CVRSystem_010::PollNextEventWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->PollNextEventOld(eOrigin, pEvent, pTrackedDevicePose);
}

bool CVRSystem_010::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_010::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}

bool CVRSystem_011::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_011::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}

bool CVRSystem_012::GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState)
{
	return base->GetControllerState(unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t));
}

bool CVRSystem_012::GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
    vr::VRControllerState_t* pControllerState, vr::TrackedDevicePose_t* pTrackedDevicePose)
{
	return base->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, sizeof(vr::VRControllerState001_t), pTrackedDevicePose);
}
