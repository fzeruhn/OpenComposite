//
// Created by ZNix on 25/10/2020.
//

#include "XrHMD.h"
#include "XrBackend.h"

#include "../OpenOVR/Misc/Config.h"
#include "../OpenOVR/Misc/LibMonado.h"
#include "../OpenOVR/Misc/xrmoreutils.h"
#include "../OpenOVR/Reimpl/BaseSystem.h"
#include "../OpenOVR/Reimpl/BaseSettings.h"
#include "../OpenOVR/Misc/SharedStore.h"
#include "../OpenOVR/convert.h"
#include "generated/static_bases.gen.h"
#include <chrono>
#include <thread>
#include <mutex>

// The session globals are briefly null while another thread restarts the session: wait up to 2 s for them. False if
// there's still no session (it was lost, or a restart failed), so the caller answers without one.
static bool WaitForSessionGlobals()
{
	// A lost session doesn't come back: no waiting for it
	auto IsLost = []() {
		BackendManager* manager = BackendManager::InstancePtr();
		auto* backend = manager ? (XrBackend*)manager->GetBackendInstance() : nullptr;
		return backend && backend->IsSessionLost();
	};
	for (int i = 0; !xr_gbl && !IsLost() && i < 100; i++) {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(20ms);
	}
	return xr_gbl != nullptr;
}

void XrHMD::GetRecommendedRenderTargetSize(uint32_t* width, uint32_t* height)
{
	// openlamina.ini's supersampleRatio, unless a program or the user's SteamVR settings chose another
	float scale = BaseSettings::RenderScale(SharedStore::Get(), oovr_global_configuration.SupersampleRatio());
	if (width)
		*width = (uint32_t)((float)xr_main_view(XruEyeLeft).recommendedImageRectWidth * scale);
	if (height)
		*height = (uint32_t)((float)xr_main_view(XruEyeLeft).recommendedImageRectHeight * scale);
}

// from BaseSystem

vr::HmdMatrix44_t XrHMD::GetProjectionMatrix(vr::EVREye eEye, float fNearZ, float fFarZ, EGraphicsAPIConvention convention)
{
	if (eEye < 0 || (int)eEye >= 2)
		eEye = vr::Eye_Left;

	if (!xr_gbl)
		return O2S_m4(glm::mat4(1.0f));
	const XruCachedViews& cachedViews = xr_gbl->GetCachedViews(xr_gbl->seatedSpace);
	const std::array<XrView, XruEyeCount>& views = cachedViews.views;
	if (cachedViews.viewCount != XruEyeCount) {
		// No views from the runtime (yet): an identity projection rather than an end
		OOVR_LOG_ONCE("GetProjectionMatrix: the OpenXR runtime gave no views, answering identity");
		return O2S_m4(glm::mat4(1.0f));
	}

	// Build the projection matrix
	// It looks like there aren't any functions in glm that can take different l/r/t/b FOV values, so do it ourselves
	// Also calculate the projection matrix as row major (so one column determines one value when multiplied with a
	// vector) and then transpose it back to being a column-major vector.
	const XrFovf& fov = views[eEye].fov;

	float tanL = tanf(fov.angleLeft);
	float tanR = tanf(fov.angleRight);
	float tanU = tanf(fov.angleUp);
	float tanD = tanf(fov.angleDown);
	float horizontalFov = -tanL + tanR;
	float verticalFov = tanU - tanD;

	// To make building these projections easier, we build them as row-major matrices then (for graphics
	// APIs that use column-major matrices) transpose them at the end if needed.

	// TODO verify!
	glm::mat4 m;
	if (convention == API_DirectX) {
		// https://docs.microsoft.com/en-us/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
		// https://www.scratchapixel.com/lessons/3d-basic-rendering/perspective-and-orthographic-projection-matrix/building-basic-perspective-projection-matrix
		// Also copied from glm::perspectiveLH_ZO
		m[0] = glm::vec4(2 / horizontalFov, 0, (tanL + tanR) / horizontalFov, 0);
		m[1] = glm::vec4(0, 2 / verticalFov, (tanU + tanD) / verticalFov, 0);
		m[2] = glm::vec4(0, 0, -fFarZ / (fFarZ - fNearZ), -(fFarZ * fNearZ) / (fFarZ - fNearZ));
		m[3] = glm::vec4(0, 0, -1, 0);

		// Don't transpose, since D3D uses row-major matricies anyway.
	} else {
		// The same off-axis frustum as above (OpenVR's matrices are row-major for every API), with OpenGL's
		// -1..1 depth range: glFrustum with l, r, t, b = near * tan(angle)
		m[0] = glm::vec4(2 / horizontalFov, 0, (tanL + tanR) / horizontalFov, 0); // First row, determines X
		m[1] = glm::vec4(0, 2 / verticalFov, (tanU + tanD) / verticalFov, 0); // Determines Y
		m[2] = glm::vec4(0, 0, -(fNearZ + fFarZ) / (fFarZ - fNearZ), -(2 * fNearZ * fFarZ) / (fFarZ - fNearZ)); // Determines Z
		m[3] = glm::vec4(0, 0, -1, 0); // Determines W
	}

	return O2S_m4(m);
}

void XrHMD::GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom)
{
	// This is how SteamVR seems to handle invalid eyes
	if (eEye < 0 || (int)eEye >= 2)
		eEye = vr::Eye_Left;

	// TODO deduplicate with GetProjectionMatrix
	static const XruCachedViews noViews{};
	const XruCachedViews& cachedViews = xr_gbl ? xr_gbl->GetCachedViews(xr_gbl->seatedSpace) : noViews;
	const std::array<XrView, XruEyeCount>& views = cachedViews.views;
	if (cachedViews.viewCount != XruEyeCount) {
		// No views from the runtime (yet): a 90 degree view rather than an end
		OOVR_LOG_ONCE("GetProjectionRaw: the OpenXR runtime gave no views, answering 90 degrees");
		if (pfLeft)
			*pfLeft = -1;
		if (pfRight)
			*pfRight = 1;
		if (pfTop)
			*pfTop = -1;
		if (pfBottom)
			*pfBottom = 1;
		return;
	}

	const XrFovf& fov = views[eEye].fov;

	/**
	 * With a straight passthrough:
	 *
	 * SteamVR Left:  -1.110925, 0.889498, -0.964926, 0.715264
	 * SteamVR Right: -1.110925, 0.889498, -0.715264, 0.964926
	 *
	 * For the Rift S:
	 * OpenXR Left: 1.000000, -1.150368, -0.965689, 1.035530
	 * SteamVR Left: -1.150368, 1.000000, -0.965689, 1.035530
	 *
	 * Via:
	 *   char buff[1024];
	 *   snprintf(buff, sizeof(buff), "eye=%d %f, %f, %f, %f", eye, *pfTop, *pfBottom, *pfLeft, *pfRight);
	 *   OOVR_LOG(buff);
	 *
	 * This suggests that SteamVR negates the top and left values, which obviously we need to match. OpenXR
	 * also negates the bottom and left value. Since it appears that SteamVR flips the top and bottom angles, we
	 * can just do that and it'll match.
	 */

	// Unfortunately this can occur on WMR, from the data not being valid very early on. See the comments in
	// GetProjectionMatrix for a further discussion of this.
	if (fov.angleDown == 0.0f && fov.angleUp == 0.0f)
		OOVR_LOG("Warning! FOV is 0");

	if (pfTop)
		*pfTop = tanf(fov.angleDown);
	if (pfBottom)
		*pfBottom = tanf(fov.angleUp);
	if (pfLeft)
		*pfLeft = tanf(fov.angleLeft);
	if (pfRight)
		*pfRight = tanf(fov.angleRight);
}

bool XrHMD::ComputeDistortion(vr::EVREye eEye, float fU, float fV, vr::DistortionCoordinates_t* pDistortionCoordinates)
{
	OOVR_SOFT_ABORT("Distortion computation not implemented for OpenXR");
	return false;
}

vr::HmdMatrix34_t XrHMD::GetEyeToHeadTransform(vr::EVREye eEye)
{
	static XrTime time = ~0; // Don't set to zero by default, otherwise we'll return an identity matrix before the first frame
	static XrView views[XruEyeCount] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };

	if (!WaitForSessionGlobals())
		return G2S_m34(glm::identity<glm::mat4>()); // no session: the eyes at the head

	// This won't exactly work for HMDs designed for spiders, but it's how SteamVR handles invalid eye numbers.
	if (eEye < 0 || (int)eEye >= 2)
		eEye = vr::Eye_Left;

	if (time != xr_gbl->GetBestTime()) {
		const XruCachedViews& cachedViews = xr_gbl->GetCachedViews(xr_gbl->viewSpace);
		const XrViewState& viewState = cachedViews.viewState;
		// Without both views the last ones stay
		if (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT && viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT
		    && cachedViews.viewCount == XruEyeCount) {
			views[0] = cachedViews.views[0];
			views[1] = cachedViews.views[1];
			time = xr_gbl->GetBestTime();
		}
	}

	return G2S_m34(X2G_om34_pose(views[eEye].pose));
}

bool XrHMD::GetTimeSinceLastVsync(float* pfSecondsSinceLastVsync, uint64_t* pulFrameCounter)
{
	// TODO: Use frame time statistics to give proper data
	OOVR_LOG_ONCE("Warning: static value returned");
	if (pfSecondsSinceLastVsync)
		*pfSecondsSinceLastVsync = 0.011f;

	return false;
}

vr::HiddenAreaMesh_t XrHMD::GetHiddenAreaMesh(vr::EVREye eEye, vr::EHiddenAreaMeshType type)
{
	if (!xr_ext->xrGetVisibilityMaskKHR_Available()) {
		// This is what the docs say we should return if the mask is unavailable
		return vr::HiddenAreaMesh_t{ nullptr, 0 };
	}

	// TODO verify the line loop mode works properly

	XrVisibilityMaskTypeKHR xrType;
	switch (type) {
	case vr::k_eHiddenAreaMesh_Standard:
		xrType = XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR;
		break;
	case vr::k_eHiddenAreaMesh_Inverse:
		xrType = XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR;
		break;
	case vr::k_eHiddenAreaMesh_LineLoop:
		xrType = XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR;
		break;
	default:
		// As SteamVR answers a mesh type it doesn't know: no mesh
		return vr::HiddenAreaMesh_t{ nullptr, 0 };
	}

	// Note: the OpenXR and OpenVR eye indexes are the same, so we can just cast between them.
	auto eye = (uint32_t)eEye;

	// First find out what size we need to allocate
	// Note that this doesn't return XR_ERROR_SIZE_INSUFFICIENT, since setting one of the input counts to zero is special-cased
	XrVisibilityMaskKHR mask = { XR_TYPE_VISIBILITY_MASK_KHR };
	if (eye >= XruEyeCount || !xr_session.get() || XR_FAILED(xr_ext->xrGetVisibilityMaskKHR(xr_session.get(), XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye, xrType, &mask))
	    || mask.indexCountOutput == 0) {
		// No mask from the runtime: what the docs say to return when there's none
		return vr::HiddenAreaMesh_t{ nullptr, 0 };
	}

	// Allocate memory for the mesh
	mask.vertexCapacityInput = mask.vertexCountOutput;
	mask.indexCapacityInput = mask.indexCountOutput;
	mask.indices = new uint32_t[mask.indexCapacityInput];
	mask.vertices = new XrVector2f[mask.vertexCapacityInput];

	// Now actually request the mask data
	if (XR_FAILED(xr_ext->xrGetVisibilityMaskKHR(xr_session.get(), XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye, xrType, &mask))) {
		delete[] mask.indices;
		delete[] mask.vertices;
		return vr::HiddenAreaMesh_t{ nullptr, 0 };
	}

	// Convert the data into something usable by SteamVR - it doesn't use indices
	vr::HiddenAreaMesh_t result = {};
	// FIXME memory allocation handling
	auto* arr = new vr::HmdVector2_t[mask.indexCountOutput];
	result.pVertexData = arr;

	float ftop, fbottom, fleft, fright;
	GetProjectionRaw(eEye, &fleft, &fright, &ftop, &fbottom);

	for (uint32_t i = 0; i < mask.indexCountOutput; i++) {
		int index = mask.indices[i];
		XrVector2f v = mask.vertices[index];

		if (oovr_global_configuration.EnableHiddenMeshFix()) {
			if (fabs(v.y - ftop) > 0.001 && fabs(v.y - fbottom) > 0.001) {
				arr[i] = vr::HmdVector2_t{ (v.x - fleft) / (fright - fleft), (v.y * oovr_global_configuration.HiddenMeshVerticalScale() - ftop) / (fbottom - ftop) };
			} else {
				arr[i] = vr::HmdVector2_t{ (v.x - fleft) / (fright - fleft), (v.y - ftop) / (fbottom - ftop) };
			}
		} else {
			arr[i] = vr::HmdVector2_t{ v.x, v.y };
		}
	}

	if (type == vr::k_eHiddenAreaMesh_LineLoop) {
		result.unTriangleCount = mask.indexCountOutput;
	} else {
		result.unTriangleCount = mask.indexCountOutput / 3;
	}

	// Delete the buffers
	delete[] mask.indices;
	delete[] mask.vertices;

	return result;
}

void XrHMD::GetPose(vr::ETrackingUniverseOrigin origin, vr::TrackedDevicePose_t* pose, ETrackingStateType trackingState)
{
	// TODO use ETrackingStateType

	// Held to the end: a session restart on another thread replaces xr_gbl and its spaces, and waits for this to end
	auto lock = xr_session.lock_shared();
	if (!xr_gbl) {
		// No session (it was lost, or a restart failed): no pose
		if (pose) {
			*pose = {};
			pose->eTrackingResult = vr::TrackingResult_Uninitialized;
		}
		return;
	}
	xr_utils::PoseFromSpace(pose, xr_gbl->viewSpace, origin);
}

float XrHMD::GetIPD()
{
	if (!xr_gbl)
		return 0.0064f;
	const XruCachedViews& cachedViews = xr_gbl->GetCachedViews(xr_gbl->viewSpace);
	const XrViewState& state = cachedViews.viewState;
	const std::array<XrView, XruEyeCount>& views = cachedViews.views;

	static float ipd = 0.0064;

	// Without both views the last IPD stays
	if (cachedViews.viewCount == XruEyeCount && state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT && state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)
		ipd = views[vr::Eye_Right].pose.position.x - views[vr::Eye_Left].pose.position.x;

	return ipd;
}

#define TRY_PROFILE_PROP(type)                                                     \
	do {                                                                           \
		if (profile) {                                                             \
			std::optional<type> ret = profile->GetProperty<type>(prop, HAND_NONE); \
			if (ret.has_value())                                                   \
				return *ret;                                                       \
		}                                                                          \
	} while (0)

// Properties
bool XrHMD::GetBoolTrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL)
{
	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

	TRY_PROFILE_PROP(bool);

	switch (prop) {
	case vr::Prop_DeviceProvidesBatteryStatus_Bool: {
		// From libmonado when Monado offers it
		std::optional<LibMonado::Battery> battery = LibMonado::Get().BatteryForRole("head");
		return battery && battery->present;
	}
	case vr::Prop_DeviceIsCharging_Bool: {
		std::optional<LibMonado::Battery> battery = LibMonado::Get().BatteryForRole("head");
		if (battery && battery->present)
			return battery->charging;
		break;
	}
	case vr::Prop_HasDriverDirectModeComponent_Bool:
		return true; // Who knows what this is used for? It's in HL:A anyway.
	case vr::Prop_ContainsProximitySensor_Bool:
		return true;
	case vr::Prop_HasCameraComponent_Bool:
		return false;
	case vr::Prop_HasDisplayComponent_Bool:
		return true;
	case vr::Prop_HasVirtualDisplayComponent_Bool:
		return false;
	default:
		break;
	}

	return XrTrackedDevice::GetBoolTrackedDeviceProperty(prop, pErrorL);
}

float XrHMD::GetFloatTrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL)
{
	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

	TRY_PROFILE_PROP(float);

	switch (prop) {
	case vr::Prop_DeviceBatteryPercentage_Float: {
		std::optional<LibMonado::Battery> battery = LibMonado::Get().BatteryForRole("head");
		if (battery && battery->present)
			return battery->charge;
		break;
	}
	case vr::Prop_DisplayFrequency_Float:
		return 90.0; // TODO use the real value
	case vr::Prop_LensCenterLeftU_Float:
	case vr::Prop_LensCenterLeftV_Float:
	case vr::Prop_LensCenterRightU_Float:
	case vr::Prop_LensCenterRightV_Float:
		// SteamVR reports it as unknown
		if (pErrorL)
			*pErrorL = vr::TrackedProp_UnknownProperty;
		return 0;
	case vr::Prop_UserIpdMeters_Float:
		return BaseSystem::SGetIpd();
	case vr::Prop_SecondsFromVsyncToPhotons_Float:
		// Seems to be used by croteam games, IDK what the real value is, 100us should do
		return 0.0001f;
	case vr::Prop_UserHeadToEyeDepthMeters_Float:
		// TODO ensure this has the correct sign, though it seems to always be zero anyway
		// In any case, see: https://github.com/ValveSoftware/openvr/issues/398
		OOVR_SOFT_ABORT("Unsupported eye relief depth setting");
		return 0;
	default:
		break;
	}

	return XrTrackedDevice::GetFloatTrackedDeviceProperty(prop, pErrorL);
}

uint32_t XrHMD::GetStringTrackedDeviceProperty(vr::ETrackedDeviceProperty prop,
    char* value, uint32_t bufferSize, vr::ETrackedPropertyError* pErrorL)
{
	std::shared_lock lock(profile_mutex);

	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

	if (profile) {
		std::optional<std::string> ret = profile->GetProperty<std::string>(prop, HAND_NONE);
		if (ret.has_value())
			return AnswerString(*ret, value, bufferSize, pErrorL);
	}

	// The rest from the HMD's row (Identity)
	return XrTrackedDevice::GetStringTrackedDeviceProperty(prop, value, bufferSize, pErrorL);
}

DeviceIdentity XrHMD::Identity()
{
	// The OpenXR system's name picks the row; read once (it doesn't change while the instance lives)
	static std::string systemName = [] {
		XrSystemProperties properties = { XR_TYPE_SYSTEM_PROPERTIES };
		if (xr_instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrGetSystemProperties(xr_instance, xr_system, &properties)))
			return std::string(properties.systemName);
		return std::string();
	}();
	return HmdIdentity(systemName);
}

int32_t XrHMD::GetInt32TrackedDeviceProperty(vr::ETrackedDeviceProperty prop, vr::ETrackedPropertyError* pErrorL)
{
	if (pErrorL)
		*pErrorL = vr::TrackedProp_Success;

	TRY_PROFILE_PROP(int32_t);

	switch (prop) {
	case vr::Prop_DeviceClass_Int32:
		return vr::TrackedDeviceClass_HMD;
	case vr::Prop_ExpectedControllerCount_Int32:
		return 2;
	case vr::Prop_ExpectedTrackingReferenceCount_Int32:
		return 0;
	default:
		break;
	}

	return ITrackedDevice::GetInt32TrackedDeviceProperty(prop, pErrorL);
}

void XrHMD::SetInteractionProfile(const InteractionProfile* profile)
{
	std::unique_lock lock(profile_mutex);
	this->profile = profile;
}
