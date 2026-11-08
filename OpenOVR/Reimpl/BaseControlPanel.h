#pragma once

#include "../BaseCommon.h"
#include "custom_interfaces/IVRMailbox_001.h"

enum OOVR_EVR_ControlPanel_Error {
	VR_ControlPanelError_None = 0,
};

class BaseControlPanel {
public:
	// IVRControlPanel_001 (SDK 0.9.0): SteamVR's own control panel's calls. There are no OpenVR drivers or displays to
	// list, other processes are the OpenXR runtime's to start and stop, and the IPD is the headset's.
	uint32_t GetDriverCount();
	uint32_t GetDriverId(uint32_t unDriverIndex, VR_OUT_STRING() char* pchBuffer, uint32_t unBufferLen);
	uint32_t GetDriverDisplayCount(const char* pchDriverId);
	uint32_t GetDriverDisplayId(const char* pchDriverId, uint32_t unDisplayIndex, VR_OUT_STRING() char* pchBuffer, uint32_t unBufferLen);
	uint32_t GetDriverDisplayModelNumber(const char* pchDriverId, const char* pchDisplayId, VR_OUT_STRING() char* pchBuffer, uint32_t unBufferLen);
	uint32_t GetDriverDisplaySerialNumber(const char* pchDriverId, const char* pchDisplayId, VR_OUT_STRING() char* pchBuffer, uint32_t unBufferLen);
	void* GetCurrentDisplayInterface(const char* pchHmdInterfaceVersion);
	void* GetCurrentCompositorInterface(const char* pchInterfaceVersion);
	uint32_t LoadSharedResource(const char* pchResourceName, char* pchBuffer, uint32_t unBufferLen);
	float GetIPD();
	void SetIPD(float fIPD);
	bool QuitProcess(uint32_t pidProcessToQuit);
	bool ForceQuitProcess(uint32_t pidProcessToQuit);
	uint32_t StartVRProcess(const char* pchExecutable, const char** pchArguments, uint32_t unArgumentCount, const char* pchWorkingDirectory);
	void SetMasterProcessToThis();
	void StartAutolaunchOverlays();
	void AbortTransition();

	void UknFunc00();
	void UknFunc01();
	void UknFunc02();
	void UknFunc03();
	void UknFunc04();
	void UknFunc05();
	void UknFunc06();
	void UknFunc07();
	void UknFunc08();
	void UknFunc09();
	void UknFunc10();
	void UknFunc11();
	void UknFunc12();
	void UknFunc13();
	void UknFunc14();
	void SetDashBoardUserToggleEnabled(bool status);
	void UknFunc16();
	void UknFunc17();
	void UknFunc18();
	void UknFunc19();
	void UknFunc20();
	void UknFunc21();
	void UknFunc22();
	void UknFunc23();
	void UknFunc24();
	void UknFunc25();
	OOVR_EVR_ControlPanel_Error RegisterExternalWebRoot();
	void UknFunc27();
	void UknFunc28();
	void UknFunc29();
};
