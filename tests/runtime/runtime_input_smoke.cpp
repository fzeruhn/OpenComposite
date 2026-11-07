// Smoke test of the stand-in runtime's actions, scripted input and optional extensions.
//   runtime_input_smoke noext   default script: enabling XR_EXT_hand_tracking is refused
//   runtime_input_smoke input   scripts/input_smoke.json: trigger value 1 from frame 5, the Index profile, hand joints
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#undef XR_USE_GRAPHICS_API_VULKAN
#undef XR_USE_GRAPHICS_API_OPENGL
#undef XR_USE_GRAPHICS_API_OPENGL_ES
#undef XR_USE_GRAPHICS_API_D3D12
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                   \
		}                                                                 \
	} while (0)
#define XR_OK(expr) CHECK(XR_SUCCEEDED(expr))

int main(int argc, char** argv)
{
	std::string mode = argc > 1 ? argv[1] : "";
	const char* exts[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME, XR_EXT_HAND_TRACKING_EXTENSION_NAME };
	XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
	std::snprintf(ici.applicationInfo.applicationName, sizeof(ici.applicationInfo.applicationName), "runtime_input_smoke");
	ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	ici.enabledExtensionCount = 2;
	ici.enabledExtensionNames = exts;
	XrInstance instance = XR_NULL_HANDLE;
	XrResult res = xrCreateInstance(&ici, &instance);
	if (mode == "noext") {
		CHECK(res == XR_ERROR_EXTENSION_NOT_PRESENT);
		std::printf("%s (%d failures)\n", failures ? "FAILED" : "passed", failures);
		return failures ? 1 : 0;
	}
	if (XR_FAILED(res)) {
		std::printf("xrCreateInstance failed: %d\n", res);
		return 1;
	}

	XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
	sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	XR_OK(xrGetSystem(instance, &sgi, &system));
	XrSystemHandTrackingPropertiesEXT htProps{ XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
	XrSystemProperties sysProps{ XR_TYPE_SYSTEM_PROPERTIES, &htProps };
	XR_OK(xrGetSystemProperties(instance, system, &sysProps));
	CHECK(htProps.supportsHandTracking == XR_TRUE);

	PFN_xrGetD3D11GraphicsRequirementsKHR getReqs = nullptr;
	XR_OK(xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReqs));
	XrGraphicsRequirementsD3D11KHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
	XR_OK(getReqs(instance, system, &reqs));
	ID3D11Device* device = nullptr;
	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr);
	CHECK(device != nullptr);
	XrGraphicsBindingD3D11KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
	binding.device = device;
	XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO, &binding, 0, system };
	XrSession session = XR_NULL_HANDLE;
	XR_OK(xrCreateSession(instance, &sci, &session));

	XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
	std::snprintf(asci.actionSetName, sizeof(asci.actionSetName), "main");
	std::snprintf(asci.localizedActionSetName, sizeof(asci.localizedActionSetName), "Main");
	XrActionSet set = XR_NULL_HANDLE;
	XR_OK(xrCreateActionSet(instance, &asci, &set));
	XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
	aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	std::snprintf(aci.actionName, sizeof(aci.actionName), "trigger");
	std::snprintf(aci.localizedActionName, sizeof(aci.localizedActionName), "Trigger");
	XrAction trigger = XR_NULL_HANDLE;
	XR_OK(xrCreateAction(set, &aci, &trigger));

	XrPath profile, triggerPath, rightHand;
	XR_OK(xrStringToPath(instance, "/interaction_profiles/valve/index_controller", &profile));
	XR_OK(xrStringToPath(instance, "/user/hand/right/input/trigger/value", &triggerPath));
	XR_OK(xrStringToPath(instance, "/user/hand/right", &rightHand));
	XrActionSuggestedBinding sb{ trigger, triggerPath };
	XrInteractionProfileSuggestedBinding ipsb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	ipsb.interactionProfile = profile;
	ipsb.countSuggestedBindings = 1;
	ipsb.suggestedBindings = &sb;
	XR_OK(xrSuggestInteractionProfileBindings(instance, &ipsb));
	XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach.countActionSets = 1;
	attach.actionSets = &set;
	XR_OK(xrAttachSessionActionSets(session, &attach));

	XrSessionBeginInfo sbi{ XR_TYPE_SESSION_BEGIN_INFO };
	sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	XR_OK(xrBeginSession(session, &sbi));

	float values[6] = {};
	for (int frame = 1; frame <= 5; frame++) {
		XrFrameState fs{ XR_TYPE_FRAME_STATE };
		XR_OK(xrWaitFrame(session, nullptr, &fs));
		XrActiveActionSet active{ set, XR_NULL_PATH };
		XrActionsSyncInfo sync{ XR_TYPE_ACTIONS_SYNC_INFO };
		sync.countActiveActionSets = 1;
		sync.activeActionSets = &active;
		XR_OK(xrSyncActions(session, &sync));
		XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
		gi.action = trigger;
		XrActionStateFloat st{ XR_TYPE_ACTION_STATE_FLOAT };
		XR_OK(xrGetActionStateFloat(session, &gi, &st));
		values[frame] = st.currentState;
		XR_OK(xrBeginFrame(session, nullptr));
		XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
		fei.displayTime = fs.predictedDisplayTime;
		fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		XR_OK(xrEndFrame(session, &fei));
	}
	CHECK(values[4] == 0.0f);
	CHECK(values[5] == 1.0f);

	XrInteractionProfileState ips{ XR_TYPE_INTERACTION_PROFILE_STATE };
	XR_OK(xrGetCurrentInteractionProfile(session, rightHand, &ips));
	CHECK(ips.interactionProfile == profile);

	PFN_xrCreateHandTrackerEXT createHand = nullptr;
	PFN_xrLocateHandJointsEXT locateHand = nullptr;
	XR_OK(xrGetInstanceProcAddr(instance, "xrCreateHandTrackerEXT", (PFN_xrVoidFunction*)&createHand));
	XR_OK(xrGetInstanceProcAddr(instance, "xrLocateHandJointsEXT", (PFN_xrVoidFunction*)&locateHand));
	CHECK(createHand && locateHand);
	if (createHand && locateHand) {
		XrHandTrackerCreateInfoEXT hci{ XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
		hci.hand = XR_HAND_RIGHT_EXT;
		hci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
		XrHandTrackerEXT tracker = XR_NULL_HANDLE;
		XR_OK(createHand(session, &hci, &tracker));
		XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		rsci.poseInReferenceSpace.orientation.w = 1;
		XrSpace stage = XR_NULL_HANDLE;
		XR_OK(xrCreateReferenceSpace(session, &rsci, &stage));
		XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
		XrHandJointLocationsEXT locs{ XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
		locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
		locs.jointLocations = joints;
		XrHandJointsLocateInfoEXT hli{ XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
		hli.baseSpace = stage;
		hli.time = 1;
		XR_OK(locateHand(tracker, &hli, &locs));
		CHECK(locs.isActive == XR_TRUE);
	}

	xrDestroyInstance(instance);
	device->Release();
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "passed", failures);
	return failures ? 1 : 0;
}
