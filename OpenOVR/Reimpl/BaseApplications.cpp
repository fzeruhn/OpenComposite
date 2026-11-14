#include "stdafx.h"
#include "Misc/EnumNames.h"
#define BASE_IMPL
#include "BaseApplications.h"
#include "BaseClientCore.h"
#include "Misc/Process.h"
#include <string>

using namespace vr;

// So I can easily use it for return types
typedef OOVR_EVRApplicationError EVRApplicationError;

// Used by Viveport
EVRApplicationError BaseApplications::AddApplicationManifest(const char* pchApplicationManifestFullPath, bool bTemporary)
{
	OOVR_LOGF("NOOP: Attempting to add manifest %s, temporary=%d", pchApplicationManifestFullPath, bTemporary);
	return VRApplicationError_None;
}
EVRApplicationError BaseApplications::RemoveApplicationManifest(const char* pchApplicationManifestFullPath)
{
	OOVR_LOGF("NOOP: Attempting to remove manifest %s", pchApplicationManifestFullPath);
	return VRApplicationError_None;
}

bool BaseApplications::IsApplicationInstalled(const char* pchAppKey)
{
	return false;
}
uint32_t BaseApplications::GetApplicationCount()
{
	return 0;
}
EVRApplicationError BaseApplications::GetApplicationKeyByIndex(uint32_t unApplicationIndex, VR_OUT_STRING() char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	if (pchAppKeyBuffer && unAppKeyBufferLen)
		pchAppKeyBuffer[0] = 0;
	// GetApplicationCount is 0
	return VRApplicationError_InvalidIndex;
}
EVRApplicationError BaseApplications::GetApplicationKeyByProcessId(uint32_t unProcessId, VR_OUT_STRING() char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	if (pchAppKeyBuffer && unAppKeyBufferLen)
		pchAppKeyBuffer[0] = 0;
	// No application list yet (IdentifyApplication isn't recorded)
	return VRApplicationError_UnknownApplication;
}
EVRApplicationError BaseApplications::LaunchApplication(const char* pchAppKey)
{
	OC_UNSUPPORTED(VRApplicationError_LaunchFailed, "LaunchApplication(%s): no application list to launch from", pchAppKey ? pchAppKey : "(null)");
}
EVRApplicationError BaseApplications::LaunchTemplateApplication(const char* pchTemplateAppKey, const char* pchNewAppKey, VR_ARRAY_COUNT(unKeys) const AppOverrideKeys_t* pKeys, uint32_t unKeys)
{
	OC_UNSUPPORTED(VRApplicationError_LaunchFailed, "LaunchTemplateApplication: no application list to launch from");
}
EVRApplicationError BaseApplications::LaunchApplicationFromMimeType(const char* pchMimeType, const char* pchArgs)
{
	OC_UNSUPPORTED(VRApplicationError_LaunchFailed, "LaunchApplicationFromMimeType: no applications are registered for MIME types");
}
EVRApplicationError BaseApplications::LaunchDashboardOverlay(const char* pchAppKey)
{
	OC_UNSUPPORTED(VRApplicationError_LaunchFailed, "LaunchDashboardOverlay: there's no dashboard");
}
bool BaseApplications::CancelApplicationLaunch(const char* pchAppKey)
{
	// Nothing is ever being launched
	return false;
}
EVRApplicationError BaseApplications::IdentifyApplication(uint32_t unProcessId, const char* pchAppKey)
{
	OOVR_LOGF("NOOP: Attempting to identify application %s, pid=%d", pchAppKey, unProcessId);
	return VRApplicationError_None;
}
uint32_t BaseApplications::GetApplicationProcessId(const char* pchAppKey)
{
	// No application list yet
	return 0;
}
const char* BaseApplications::GetApplicationsErrorNameFromEnum(EVRApplicationError error)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRApplicationError", (int32_t)error, "Unknown VRApplicationsError (%d)");
}
uint32_t BaseApplications::GetApplicationPropertyString(const char* pchAppKey, EVRApplicationProperty eProperty, VR_OUT_STRING() char* pchPropertyValueBuffer, uint32_t unPropertyValueBufferLen, EVRApplicationError* peError)
{
	if (pchPropertyValueBuffer && unPropertyValueBufferLen)
		pchPropertyValueBuffer[0] = 0;
	if (peError)
		*peError = VRApplicationError_UnknownApplication;
	return 0;
}
bool BaseApplications::GetApplicationPropertyBool(const char* pchAppKey, EVRApplicationProperty eProperty, EVRApplicationError* peError)
{
	if (peError)
		*peError = VRApplicationError_UnknownApplication;
	return false;
}
uint64_t BaseApplications::GetApplicationPropertyUint64(const char* pchAppKey, EVRApplicationProperty eProperty, EVRApplicationError* peError)
{
	if (peError)
		*peError = VRApplicationError_UnknownApplication;
	return 0;
}
EVRApplicationError BaseApplications::SetApplicationAutoLaunch(const char* pchAppKey, bool bAutoLaunch)
{
	// No application list yet: every key is unknown
	return VRApplicationError_UnknownApplication;
}
bool BaseApplications::GetApplicationAutoLaunch(const char* pchAppKey)
{
	return false;
}
EVRApplicationError BaseApplications::SetDefaultApplicationForMimeType(const char* pchAppKey, const char* pchMimeType)
{
	// No application list yet: every key is unknown
	return VRApplicationError_UnknownApplication;
}
bool BaseApplications::GetDefaultApplicationForMimeType(const char* pchMimeType, VR_OUT_STRING() char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	if (pchAppKeyBuffer && unAppKeyBufferLen)
		pchAppKeyBuffer[0] = 0;
	return false;
}
bool BaseApplications::GetApplicationSupportedMimeTypes(const char* pchAppKey, VR_OUT_STRING() char* pchMimeTypesBuffer, uint32_t unMimeTypesBuffer)
{
	if (pchMimeTypesBuffer && unMimeTypesBuffer)
		pchMimeTypesBuffer[0] = 0;
	return false;
}
uint32_t BaseApplications::GetApplicationsThatSupportMimeType(const char* pchMimeType, VR_OUT_STRING() char* pchAppKeysThatSupportBuffer, uint32_t unAppKeysThatSupportBuffer)
{
	if (pchAppKeysThatSupportBuffer && unAppKeysThatSupportBuffer)
		pchAppKeysThatSupportBuffer[0] = 0;
	return 0;
}
uint32_t BaseApplications::GetApplicationLaunchArguments(uint32_t unHandle, VR_OUT_STRING() char* pchArgs, uint32_t unArgs)
{
	if (pchArgs && unArgs)
		pchArgs[0] = 0;
	// Nothing launches applications with arguments
	return 0;
}
EVRApplicationError BaseApplications::GetStartingApplication(VR_OUT_STRING() char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	if (pchAppKeyBuffer && unAppKeyBufferLen)
		pchAppKeyBuffer[0] = 0;
	// Nothing is being launched
	return VRApplicationError_NoApplication;
}
BaseApplications::EVRApplicationTransitionState BaseApplications::GetTransitionState()
{
	return VRApplicationTransition_None;
}
BaseApplications::EVRSceneApplicationState BaseApplications::GetSceneApplicationState()
{
	// This program is the scene application while it has a Scene session
	return BaseClientCore::appType == VRApplication_Scene ? EVRSceneApplicationState_Running : EVRSceneApplicationState_None;
}
EVRApplicationError BaseApplications::PerformApplicationPrelaunchCheck(const char* pchAppKey)
{
	// No application list yet: every key is unknown
	return VRApplicationError_UnknownApplication;
}
const char* BaseApplications::GetApplicationsTransitionStateNameFromEnum(EVRApplicationTransitionState state)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRApplicationTransitionState", (int32_t)state, "Unknown EVRApplicationTransitionState");
}
const char* BaseApplications::GetSceneApplicationStateNameFromEnum(BaseApplications::EVRSceneApplicationState state)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRSceneApplicationState", (int32_t)state, "Unknown EVRSceneApplicationState (%d)");
}
bool BaseApplications::IsQuitUserPromptRequested()
{
	return false;
}
EVRApplicationError BaseApplications::LaunchInternalProcess(const char* pchBinaryPath, const char* pchArguments, const char* pchWorkingDirectory)
{
	if (!pchBinaryPath)
		return VRApplicationError_LaunchFailed;
#ifndef _WIN32
	// Only the lab uses this afaik, we should be pretty safe
	OC_UNSUPPORTED(VRApplicationError_LaunchFailed, "LaunchInternalProcess: not on Linux yet");
#else
	OOVR_LOG("Launching new app process: following values are path,args,workingDir:");
	OOVR_LOG(pchBinaryPath);
	OOVR_LOG(pchArguments ? pchArguments : "");
	OOVR_LOG(pchWorkingDirectory ? pchWorkingDirectory : "");

	string cmd = "\"" + string(pchBinaryPath) + "\" " + string(pchArguments ? pchArguments : "");
	size_t buff_len = cmd.length() + 1;
	char* cmd_c = new char[buff_len];
	strcpy_s(cmd_c, buff_len, cmd.c_str());

	STARTUPINFOA si;
	PROCESS_INFORMATION pi;

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	memset(&pi, 0, sizeof(pi));

	PROCESS_INFORMATION processInfo;
	if (CreateProcessA(pchBinaryPath, cmd_c, NULL, NULL, TRUE, 0, NULL, pchWorkingDirectory, &si, &processInfo)) {
		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
		return VRApplicationError_None;
	}

	return VRApplicationError_LaunchFailed;
#endif
}
uint32_t BaseApplications::GetCurrentSceneProcessId()
{
	return BaseClientCore::appType == VRApplication_Scene ? oc_current_pid() : 0;
}

EVRApplicationError BaseApplications::RegisterSubprocess(uint32_t nPid)
{
	// On success the caller exits, as it would with SteamVR. That's fine here: the subprocess
	// loads OpenComposite itself and calls VR_Init on its own, and there's no transition UI to hide.
	return VRApplicationError_None;
}

EVRApplicationError BaseApplications::GetHomeApplication(char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	if (pchAppKeyBuffer && unAppKeyBufferLen)
		pchAppKeyBuffer[0] = '\0';
	return VRApplicationError_UnknownApplication;
}

EVRApplicationError BaseApplications::SetHomeApplication(const char* pchAppKey)
{
	OC_UNSUPPORTED(VRApplicationError_UnknownApplication, "SetHomeApplication (IVRApplications_001): there's no home "
	                                                      "application on OpenXR");
}
