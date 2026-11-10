// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include "plat.h"

#include "Reimpl/BaseSystem.h"
#include "Reimpl/Interfaces.h"

// Needed for the system-wide usage of this DLL (when renamed to vrclient[_x64].dll)
#include "generated/GVRClientCore.gen.h"
#include "generated/version.h"

#include "Misc/backtrace.h"
#include "logging.h"
#include "steamvr_abi.h"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "Misc/EnumNames.h"

// Specific to OCOVR
#include "Drivers/Backend.h"
#include "DrvOpenXR.h"

using namespace vr;

// Binary-compatible openvr_api.dll implementation
static bool running;
static bool running_ovr; // are we in an apptype which uses LibOVR?
static uint32_t current_init_token = 1;
static EVRApplicationType current_apptype;
// Inits of a running Scene app not yet matched by a shutdown (a second Init succeeds and is counted)
static int scene_init_count = 0;
// A Utility app has called Init: it may have the interfaces whose APPTYPE flag allows Utility, with no backend
static bool utility_init = false;
// Unknown interface names already logged (games ask from several threads)
static std::set<std::string> logged_unknown_interfaces;
static std::mutex logged_unknown_interfaces_mutex;

static bool FirstAskForUnknown(const char* interfaceVersion)
{
	std::lock_guard<std::mutex> lock(logged_unknown_interfaces_mutex);
	return logged_unknown_interfaces.insert(interfaceVersion).second;
}

static alternativeCoreFactory_t alternativeCoreFactory = nullptr;

class _InheritCVRLayout {
	virtual void _ignore() = 0;
};
class CVRCorrectLayout : public _InheritCVRLayout, public CVRCommon {
};

// If we don't set up our own deleter, then at least on linux it'll get the layout mixed up and call a random function
// Also for the basis of this typedef, see: https://stackoverflow.com/a/26276805
using correct_layout_unique = std::unique_ptr<CVRCorrectLayout, std::function<void(CVRCorrectLayout*)>>;

static std::map<std::string, correct_layout_unique> interfaces;

VR_INTERFACE void* VR_CALLTYPE VR_GetGenericInterface(const char* interfaceVersion, EVRInitError* error)
{
	if (!interfaceVersion) {
		if (error)
			*error = VRInitError_Init_InvalidInterface;
		return NULL;
	}

	if (!running && !utility_init) {
		OOVR_LOGF("[INFO] VR_GetGenericInterface called while OOVR not running, setting error=NotInitialized, for interfaceVersion=%s", interfaceVersion);
		if (error)
			*error = VRInitError_Init_NotInitialized;
		return NULL;
	}

	// Unless we later change this otherwise, it was successful.
	// Oh, and who would pass in a null error pointer, since they're only called by `openvr_api.dll` which
	// defines all of them? HL:A of course!
	if (error)
		*error = VRInitError_None;

	// First check if they're getting the 'FnTable' version of this interface.
	// This is a table of methods, but critically they *don't* take a 'this' pointer,
	//  so we can't cheat and return the vtable.
	const char* fnTableStr = "FnTable:";
	if (!strncmp(fnTableStr, interfaceVersion, strlen(fnTableStr))) {
		const char* baseInterface = interfaceVersion + strlen(fnTableStr);

		// Get the C++ interface
		// Note we can't directly cast to CVRCommon, as we'll then be referring to the OpenVR interface
		// vtable - look up how vtables work with multiple inheritance if you're confused about this.
		CVRCorrectLayout* interfaceClass = (CVRCorrectLayout*)VR_GetGenericInterface(baseInterface, error);

		// If the interface is NULL, then error will have been set and we can return null too.
		if (!interfaceClass) {
			return NULL;
		}

		return interfaceClass->_GetStatFuncList();
	}

	if (interfaces.count(interfaceVersion)) {
		return interfaces[interfaceVersion].get();
	}

	// An interface we don't have (and SteamVR-internal ones such as IVRIPCResourceManagerClient_*, IXrProto_001, or
	// the retired IVROCSystem_001) is answered "not found", as SteamVR answers a version it doesn't have: games check
	// the error and carry on without it (Half-Life: Alyx does)
	bool valid_apptypes_success;
	uint64_t valid_apptypes = GetInterfaceFlagsByName(interfaceVersion, "APPTYPE", &valid_apptypes_success);
	void* probe = nullptr;
	if (!valid_apptypes_success) {
		// GetInterfaceFlagsByName only knows the interfaces we have: try making it to tell "unknown" apart
		probe = CreateInterfaceByName(interfaceVersion);
		if (!probe) {
			if (FirstAskForUnknown(interfaceVersion))
				OOVR_LOGF("Interface %s isn't offered: answering InterfaceNotFound", interfaceVersion);
			if (error)
				*error = VRInitError_Init_InterfaceNotFound;
			return NULL;
		}
		valid_apptypes = 1ull << VRApplication_Scene;
#ifndef _WIN32
		valid_apptypes |= (1ull << VRApplication_Background); // Proton uses this
#endif
	}

	EVRApplicationType apptype = utility_init && !running ? VRApplication_Utility : current_apptype;
	if ((valid_apptypes & (1ull << apptype)) == 0) {
		OOVR_LOGF("Interface %s isn't available to application type %d", interfaceVersion, apptype);
		if (probe)
			((CVRCorrectLayout*)probe)->Delete();
		if (error)
			*error = apptype == VRApplication_Utility ? VRInitError_Init_NotAvailableToUtilityApps : VRInitError_Init_InvalidInterface;
		return NULL;
	}

	CVRCorrectLayout* impl = (CVRCorrectLayout*)(probe ? probe : CreateInterfaceByName(interfaceVersion));
	if (impl) {
		correct_layout_unique ptr(impl, [](CVRCorrectLayout* cl) {
			cl->Delete();
		});
		interfaces[interfaceVersion] = std::move(ptr);
		return impl;
	}

	if (FirstAskForUnknown(interfaceVersion))
		OOVR_LOGF("Interface %s isn't offered: answering InterfaceNotFound", interfaceVersion);
	if (error)
		*error = VRInitError_Init_InterfaceNotFound;
	return NULL;
#undef INTERFACE
}

// Valve's openvr_api.dll also exports this accessor, and Half-Life: Alyx's vr.dll imports it
// (VR_GetGenericInterface caches the instance, and frees it at shutdown, so don't hold on to it here)
VR_INTERFACE void* VR_CALLTYPE VRControlPanel()
{
	EVRInitError error = VRInitError_None;
	return VR_GetGenericInterface("IVRControlPanel_006", &error);
}

VR_INTERFACE uint32_t VR_CALLTYPE VR_GetInitToken()
{
	return current_init_token;
}

VR_INTERFACE const char* VR_CALLTYPE VR_GetVRInitErrorAsEnglishDescription(EVRInitError error);

VR_INTERFACE char* VR_GetStringForHmdError(int err)
{
	return const_cast<char*>(VR_GetVRInitErrorAsEnglishDescription((EVRInitError)err));
}

VR_INTERFACE const char* VR_CALLTYPE VR_GetVRInitErrorAsEnglishDescription(EVRInitError error)
{
	switch (error) {
	case VRInitError_None:
		return "None";

		// Copied over from SteamVR, so these won't cause issues by not matching their SteamVR equivilents:
		// clang-format off
	case 100: return "Installation Not Found (100)";
	case 101: return "Installation Corrupt (101)";
	case 102: return "vrclient Shared Lib Not Found (102)";
	case 103: return "File Not Found (103)";
	case 104: return "Factory Function Not Found (104)";
	case 105: return "Interface Not Found (105)";
	case 106: return "Invalid Interface (106)";
	case 107: return "User Config Directory Invalid (107)";
	case 108: return "Hmd Not Found (108)";
	case 109: return "Not Initialized (109)";
	case 110: return "Installation path could not be located (110)";
	case 111: return "Config path could not be located (111)";
	case 112: return "Log path could not be located (112)";
	case 113: return "Unable to write path registry (113)";
	case 114: return "App info manager init failed (114)";
	case 115: return "Internal Retry (115)";
	case 116: return "User Canceled Init (116)";
	case 117: return "Another app was already launching (117)";
	case 118: return "Settings manager init failed (118)";
	case 119: return "VR system shutting down (119)";
	case 120: return "Too many tracked objects (120)";
	case 121: return "Not starting vrserver for background app (121)";
	case 122: return "The requested interface is incompatible with the compositor and the compositor is running (122)";
	case 123: return "This interface is not available to utility applications (123)";
	case 124: return "vrserver internal error (124)";
	case 125: return "Hmd DriverId is invalid (125)";
	case 126: return "Hmd Not Found Presence Failed (126)";
	case 127: return "VR Monitor Not Found (127)";
	case 128: return "VR Monitor startup failed (128)";
	case 129: return "Low Power Watchdog Not Supported (129)";
	case 130: return "Invalid Application Type (130)";
	case 131: return "Not available to watchdog apps (131)";
	case 132: return "Watchdog disabled in settings (132)";
	case 133: return "VR Dashboard Not Found (133)";
	case 134: return "VR Dashboard startup failed (134)";
	case 135: return "VR Home Not Found (135)";
	case 136: return "VR home startup failed (136)";
	case 137: return "Rebooting In Progress (137)";
	case 138: return "Firmware Update In Progress (138)";
	case 139: return "Firmware Recovery In Progress (139)";
	case 140: return "USB Service Busy (140)";
	case 141: return "VRInitError_Init_VRWebHelperStartupFailed";
	case 142: return "VRInitError_Init_TrackerManagerInitFailed";
	case 143: return "VRInitError_Init_AlreadyRunning";
	case 144: return "VRInitError_Init_FailedForVrMonitor";
	case 145: return "VRInitError_Init_PropertyManagerInitFailed";
	case 146: return "VRInitError_Init_WebServerFailed";
		// clang-format on

	default:
		return "VRInitError_Init_IllegalTypeTransition";
	}
}

VR_INTERFACE const char* VR_CALLTYPE VR_GetVRInitErrorAsSymbol(EVRInitError error)
{
	const char* name = OC_EnumName("EVRInitError", (int32_t)error);
	return name ? name : "Unknown";
}

VR_INTERFACE uint32_t VR_CALLTYPE VR_InitInternal(EVRInitError* peError, EVRApplicationType eApplicationType)
{
	return VR_InitInternal2(peError, eApplicationType, NULL);
}

VR_INTERFACE uint32_t VR_CALLTYPE VR_InitInternal2(EVRInitError* peError, EVRApplicationType eApplicationType, const char* pStartupInfo)
{
	OOVR_LOG_ONCEF("Initializing OpenComposite - revision %s", OC_VERSION);
	if (peError) {
		*peError = VRInitError_None;
	}

	oovr_install_crash_handler();

	if (eApplicationType == VRApplication_Bootstrapper) {
#ifdef _WIN32
		char szFileName[MAX_PATH];
		GetModuleFileNameA(NULL, szFileName, MAX_PATH);
		string filename = szFileName;

		filename.erase(0, filename.find_last_of('\\') + 1);

		if (filename == "vrstartup.exe") {
			// AFAIK vrstartup is the only program that uses this mode, and returning an
			// error makes it exit immediately.
			if (peError)
				*peError = VRInitError_Unknown;
			return 0;
		}
#endif
		OOVR_LOG("Refusing the Bootstrapper application type: only vrstartup.exe uses it");
		if (peError)
			*peError = VRInitError_Init_InvalidApplicationType;
		return 0;
	}

	if (eApplicationType == VRApplication_Utility) {
		if (running) {
			// Counted like a second Init, so its Shutdown doesn't end the running session
			scene_init_count++;
			return current_init_token;
		}
		// No backend: only the interfaces a Utility app may have (APPTYPE flag), until its Shutdown
		utility_init = true;
		BaseClientCore::appType = eApplicationType;
		return current_init_token;
	}

	if (eApplicationType != VRApplication_Scene)
#ifndef _WIN32
		if (eApplicationType != VRApplication_Background) // Proton uses this
#endif
		{
			// Overlays and background apps aren't games, so refuse them with an init error rather than ending
			// the program: it can then carry on as if VR weren't there.
			OOVR_LOGF("Refusing application type %d: OpenComposite runs games only", (int)eApplicationType);
			if (peError) {
				*peError = eApplicationType == VRApplication_Background ? VRInitError_Init_NoServerForBackgroundApp : VRInitError_Init_InvalidApplicationType;
			}
			return 0;
		}

	if (running) {
		// A second Init while running keeps the running session; each Init is matched by a shutdown
		OOVR_LOG("Init while already running: keeping the running session");
		scene_init_count++;
		return current_init_token;
	}

	running_ovr = true;

	current_apptype = eApplicationType;
	BaseClientCore::appType = eApplicationType;
	running = true;

	// TODO seperate this from the rest of dllmain
	EVRInitError backendError = VRInitError_None;
	IBackend* backend = DrvOpenXR::CreateOpenXRBackend(pStartupInfo, &backendError);
	if (!backend) {
		if (peError)
			*peError = backendError != VRInitError_None ? backendError : VRInitError_Init_Internal;
		running = false;
		running_ovr = false;
		BaseClientCore::appType = utility_init ? VRApplication_Utility : VRApplication_Max;
		return 0;
	}
	BackendManager::Create(backend);
	// An earlier Utility Init not yet shut down is matched by a Shutdown too
	scene_init_count = utility_init ? 2 : 1;
	utility_init = false;

	return current_init_token;
}

VR_INTERFACE bool VR_CALLTYPE VR_IsHmdPresent()
{
	// If we're already running, then Oculus's implementation fails with XR_ERROR_RUNTIME_FAILURE
	if (running)
		return true;

	// At this point, we should NOT be running OpenXR, so setup a new session just for this
	if (xr_instance != XR_NULL_HANDLE)
		return true; // an instance from an earlier Init is still up: it found a headset then

	// FIXME copied from DrvOpenXR
	XrApplicationInfo appInfo{};
	DrvOpenXR::GetXRAppName(appInfo.applicationName);
	appInfo.applicationVersion = 1;
	appInfo.apiVersion = XR_CURRENT_API_VERSION;
	XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
	createInfo.applicationInfo = appInfo;
	XrInstance tmp_instance;
	if (xrCreateInstance(&createInfo, &tmp_instance) != XR_SUCCESS) {
		return false;
	}

	XrSystemGetInfo systemInfo{};
	systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId id;
	XrResult res = xrGetSystem(tmp_instance, &systemInfo, &id);

	xrDestroyInstance(tmp_instance);

	if (res == XR_SUCCESS)
		return true;

	// FORM_FACTOR_UNAVAILABLE (no headset plugged in) and anything else: no headset to start (upstream #467)
	if (res != XR_ERROR_FORM_FACTOR_UNAVAILABLE && res != XR_ERROR_FORM_FACTOR_UNSUPPORTED)
		OOVR_LOGF("Failed to probe for OpenXR systems: return status %d", res);
	return false;
}

// Necessary for Proton games on Linux: https://github.com/ValveSoftware/Proton/blob/22f40122788d5f65389f59144705309ce9d6d403/vrclient_x64/vrclient_x64/vrclient_main.c#L267
// If this symbol isn't found, games will get VRInitError_Init_InterfaceNotFound - too tired to remember if it's the Proton vrclient that's doing this or openvr_api that's doing this.
// Either way you need this.
VR_INTERFACE uint32_t VR_CALLTYPE HmdSystemFactory()
{
	OOVR_LOG("HmdSystemFactory called - this is only here to make Proton recognise us, should never be called! Please file a bug, this is interesting.");
	return 0;
}

VR_INTERFACE bool VR_CALLTYPE VR_IsInterfaceVersionValid(const char* pchInterfaceVersion)
{
	return true; // Kinda dodgy
}

VR_INTERFACE bool VR_CALLTYPE VR_IsRuntimeInstalled()
{
	// TODO in future check that the Oculus Runtime is installed
	return true;
}

VR_INTERFACE const char* VR_CALLTYPE VR_RuntimePath()
{
	// The folder our bin\ folder is in, as the paths file names it
	static std::string path = [] {
		// As UTF-8, as OpenVR wants: string() throws on Windows for characters outside the ANSI code page
		try {
			std::filesystem::path bin = BaseClientCore::GetDllDir(); // ends with a separator
			if (!bin.has_filename())
				bin = bin.parent_path();
			// The Linux libraries are in a folder under bin/ (linux64/, linuxarm64/)
			if (bin.filename() != "bin" && bin.parent_path().filename() == "bin")
				bin = bin.parent_path();
			std::u8string u = bin.parent_path().u8string();
			return std::string(u.begin(), u.end());
		} catch (const std::exception&) {
			return std::string();
		}
	}();
	return path.c_str();
}

VR_INTERFACE void VR_CALLTYPE VR_ShutdownInternal()
{
	if (utility_init && !running) {
		utility_init = false;
		BaseClientCore::appType = VRApplication_Max;
		interfaces.clear();
		return;
	}
	if (!running) {
		OOVR_LOG("Shutdown while not running: ignored");
		return;
	}
	// A Shutdown after a refused Init (say a plugin's Background one) can't be told from the game's own, so it's counted
	if (scene_init_count > 1) {
		scene_init_count--;
		OOVR_LOGF("Shutdown with %d more Init(s) to match: keeping the session", scene_init_count);
		return;
	}
	scene_init_count = 0;
	utility_init = false;

	OOVR_LOG("OpenComposite shutdown");

	// What the app asked for that OpenComposite doesn't do, with counts
	oc_soft_log_summary();

	// Reset interfaces
	// Do this first, while the OVR session is still available in case they
	//  need to use it for cleanup.
	interfaces.clear();

	// Shut down OpenXR
	BackendManager::Reset();

	// The origin outlives the interfaces (SetTrackingSpace may come without IVRSystem): the next Init starts on the default
	BaseSystem::currentSpace = XR_REFERENCE_SPACE_TYPE_STAGE;

	running = false;
	BaseClientCore::appType = VRApplication_Max;
}

VR_INTERFACE void* VRClientCoreFactory(const char* pInterfaceName, int* pReturnCode)
{
	if (alternativeCoreFactory) {
	use_alt:
		return alternativeCoreFactory(pInterfaceName, pReturnCode);
	}

	bool shouldUseOC = BaseClientCore::CheckAppEnabled();
	if (!shouldUseOC) {
		alternativeCoreFactory = PlatformGetAlternativeCoreFactory();
		if (alternativeCoreFactory)
			goto use_alt;
		// The app was set to SteamVR, which couldn't be loaded (logged): the game still starts, on OpenComposite
		OOVR_LOG("SteamVR was chosen for this program but couldn't be loaded; using OpenComposite instead");
	}

	if (pReturnCode)
		*pReturnCode = VRInitError_None;
	if (!pInterfaceName) {
		if (pReturnCode)
			*pReturnCode = VRInitError_Init_InterfaceNotFound;
		return nullptr;
	}

	string name = pInterfaceName;

#define CLIENT_VER(ver)                                       \
	if (IVRClientCore_##ver::IVRClientCore_Version == name) { \
		static CVRClientCore_##ver inst;                      \
		return &inst;                                         \
	}

	CLIENT_VER(002);
	CLIENT_VER(003);

#undef CLIENT_VER

	// As SteamVR answers a client core version it doesn't have
	OOVR_LOGF("Client core interface %s isn't offered: answering InterfaceNotFound", pInterfaceName);
	if (pReturnCode)
		*pReturnCode = VRInitError_Init_InterfaceNotFound;
	return nullptr;
}
