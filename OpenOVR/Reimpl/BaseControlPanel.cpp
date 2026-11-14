#include "stdafx.h"
#define BASE_IMPL
#include "BaseControlPanel.h"

// These functions are used in HL:A - See #176
// Nooping both of these should be fine.
void BaseControlPanel::SetDashBoardUserToggleEnabled(bool status)
{
}
OOVR_EVR_ControlPanel_Error BaseControlPanel::RegisterExternalWebRoot()
{
	return VR_ControlPanelError_None;
}

// Unknown functions
// SteamVR's own (undocumented) control panel calls: nothing that a game relies on
#define UKN_FUNC(id)                                                       \
	void BaseControlPanel::UknFunc##id()                                   \
	{                                                                      \
		OC_UNSUPPORTED_VOID("IVRControlPanel: unknown function " #id); \
	}

UKN_FUNC(00);
UKN_FUNC(01);
UKN_FUNC(02);
UKN_FUNC(03);
UKN_FUNC(04);
UKN_FUNC(05);
UKN_FUNC(06);
UKN_FUNC(07);
UKN_FUNC(08);
UKN_FUNC(09);
UKN_FUNC(10);
UKN_FUNC(11);
UKN_FUNC(12);
UKN_FUNC(13);
UKN_FUNC(14);
UKN_FUNC(16);
UKN_FUNC(17);
UKN_FUNC(18);
UKN_FUNC(19);
UKN_FUNC(20);
UKN_FUNC(21);
UKN_FUNC(22);
UKN_FUNC(23);
UKN_FUNC(24);
UKN_FUNC(25);
UKN_FUNC(27);
UKN_FUNC(28);
UKN_FUNC(29);

#include "BaseClientCore.h"
#include "Misc/Process.h"
#include "BaseSystem.h"
#include "generated/static_bases.gen.h"

static uint32_t EmptyString(char* pchBuffer, uint32_t unBufferLen)
{
	if (pchBuffer && unBufferLen)
		pchBuffer[0] = '\0';
	return 0;
}

uint32_t BaseControlPanel::GetDriverCount()
{
	return 0;
}

uint32_t BaseControlPanel::GetDriverId(uint32_t unDriverIndex, char* pchBuffer, uint32_t unBufferLen)
{
	return EmptyString(pchBuffer, unBufferLen);
}

uint32_t BaseControlPanel::GetDriverDisplayCount(const char* pchDriverId)
{
	return 0;
}

uint32_t BaseControlPanel::GetDriverDisplayId(const char* pchDriverId, uint32_t unDisplayIndex, char* pchBuffer, uint32_t unBufferLen)
{
	return EmptyString(pchBuffer, unBufferLen);
}

uint32_t BaseControlPanel::GetDriverDisplayModelNumber(const char* pchDriverId, const char* pchDisplayId, char* pchBuffer, uint32_t unBufferLen)
{
	return EmptyString(pchBuffer, unBufferLen);
}

uint32_t BaseControlPanel::GetDriverDisplaySerialNumber(const char* pchDriverId, const char* pchDisplayId, char* pchBuffer, uint32_t unBufferLen)
{
	return EmptyString(pchBuffer, unBufferLen);
}

void* BaseControlPanel::GetCurrentDisplayInterface(const char* pchHmdInterfaceVersion)
{
	// The interface asked for, as VR_GetGenericInterface gives it (null for one OpenComposite doesn't offer)
	if (!pchHmdInterfaceVersion)
		return nullptr;
	vr::EVRInitError err = vr::VRInitError_None;
	return GetCreateBaseClientCore()->GetGenericInterface(pchHmdInterfaceVersion, &err);
}

void* BaseControlPanel::GetCurrentCompositorInterface(const char* pchInterfaceVersion)
{
	return GetCurrentDisplayInterface(pchInterfaceVersion);
}

uint32_t BaseControlPanel::LoadSharedResource(const char* pchResourceName, char* pchBuffer, uint32_t unBufferLen)
{
	// SteamVR's shared resources (its own images and sounds) aren't here: not found
	return 0;
}

float BaseControlPanel::GetIPD()
{
	return BaseSystem::SGetIpd();
}

void BaseControlPanel::SetIPD(float fIPD)
{
	OC_UNSUPPORTED_VOID("SetIPD (IVRControlPanel_001): the IPD is the headset's");
}

bool BaseControlPanel::QuitProcess(uint32_t pidProcessToQuit)
{
	// SteamVR sends the process a quit request and answers true, whatever the process. This process gets VREvent_Quit;
	// another isn't OpenComposite's to reach
	if (pidProcessToQuit == oc_current_pid()) {
		BaseSystem* system = GetUnsafeBaseSystem();
		if (system) {
			vr::VREvent_t quit = {};
			quit.eventType = vr::VREvent_Quit;
			quit.trackedDeviceIndex = vr::k_unTrackedDeviceIndexInvalid;
			quit.data.process.pid = pidProcessToQuit;
			system->_EnqueueEvent(quit);
		}
		return true;
	}
	OC_UNSUPPORTED(true, "QuitProcess (IVRControlPanel_001): another process isn't OpenComposite's to quit");
}

bool BaseControlPanel::ForceQuitProcess(uint32_t pidProcessToQuit)
{
	OC_UNSUPPORTED(false, "ForceQuitProcess (IVRControlPanel_001): other processes aren't OpenComposite's to quit");
}

uint32_t BaseControlPanel::StartVRProcess(const char* pchExecutable, const char** pchArguments, uint32_t unArgumentCount, const char* pchWorkingDirectory)
{
	// 0: no process started
	OC_UNSUPPORTED(0, "StartVRProcess (IVRControlPanel_001): starting VR processes is the OpenXR runtime's");
}

void BaseControlPanel::SetMasterProcessToThis()
{
	// There's no master process to hand over: each OpenComposite app is its own
}

void BaseControlPanel::StartAutolaunchOverlays()
{
	// SteamVR's autolaunch overlays aren't installed here: none to start
}

void BaseControlPanel::AbortTransition()
{
	// No transition between scene apps is in progress (each runs on its own)
}
