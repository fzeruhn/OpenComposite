#include "stdafx.h"
#include <cmath>
#include <openxr/openxr.h>
#define BASE_IMPL
#include "../../DrvOpenXR/XrBackend.h"
#include "BaseCompositor.h"
#include "BaseOverlay.h"

#include "Misc/EnumNames.h"
#include "BaseSystem.h"
#include "Compositor/compositor.h"
#include "Drivers/Backend.h"
#include "Misc/Config.h"
#include "Misc/ScopeGuard.h"
#include "convert.h"
#include "generated/static_bases.gen.h"
#include <string>

using glm::mat4;
using glm::vec3;

using namespace vr;

// Class to represent an overlay
class BaseOverlay::OverlayData {
public:
	const string key;
	string name;
	HmdColor_t colour;

	float widthMeters = 1; // default 1 meter
	float curvature = 0;

	float autoCurveDistanceRangeMin, autoCurveDistanceRangeMax; // WTF does this do?
	EColorSpace colourSpace = ColorSpace_Auto;
	bool visible = false; // TODO check against SteamVR
	VRTextureBounds_t textureBounds = { 0, 0, 1, 1 };
	VROverlayInputMethod inputMethod = VROverlayInputMethod_None; // TODO fire events
	HmdVector2_t mouseScale = { 1.0f, 1.0f };
	bool highQuality = false;
	uint64_t flags = 0;
	float texelAspect = 1;
	std::queue<VREvent_t> eventQueue;

	// Kept and returned, not shown yet (drawing or using them is not implemented yet)
	uint32_t renderingPid = 0;
	float preCurvePitch = 0;
	std::string renderModel;
	HmdColor_t renderModelColor = { 1, 1, 1, 1 };
	TrackedDeviceIndex_t componentDevice = k_unTrackedDeviceIndexInvalid;
	std::string componentName;
	VROverlayHandle_t relativeParent = k_ulOverlayHandleInvalid;
	HmdMatrix34_t relativeTransform = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
	HmdVector2_t cursorHotspot = { 0, 0 };
	VROverlayHandle_t cursor = k_ulOverlayHandleInvalid;
	bool cursorOverridden = false;
	HmdVector2_t cursorOverride = { 0, 0 };
	std::map<int, VROverlayHandle_t> neighbours; // EOverlayDirection → overlay
	std::map<int, std::pair<HmdVector2_t, float>> dualAnalog; // EDualAnalogWhich → (centre, radius)

	// Rendering
	Texture_t texture = {};

	union {
		XrCompositionLayerBaseHeader base;
		XrCompositionLayerQuad quad;
		XrCompositionLayerCylinderKHR cylinder;
	} compositionLayer;

	std::weak_ptr<Compositor> compositor;
	float distance;
	uint32_t sortOrder = 0;

	// Transform
	VROverlayTransformType transformType = VROverlayTransform_Absolute;
	union {
		struct {
			HmdMatrix34_t offset;
			TrackedDeviceIndex_t device;
		} deviceRelative;
	} transformData;

	glm::mat4 overlayTransform{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, -1.01f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	OverlayData(string key, string name)
	    : key(key), name(name)
	{
		memset(&compositionLayer, 0, sizeof(compositionLayer));
	}

	// The overlay's transform in the tracking space, stored like overlayTransform (rows first: translation in [i][3])
	bool ComputeOverlayTransform(vr::ETrackingUniverseOrigin trackingSpace, glm::mat4& out)
	{
		out = overlayTransform;
		// A component's overlay is placed at its device, with no offset (the component's own pose isn't known)
		bool component = transformType == VROverlayTransform_TrackedComponent;
		if (transformType == VROverlayTransform_TrackedDeviceRelative || component) {
			std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDevice(component ? componentDevice : transformData.deviceRelative.device);
			if (!dev)
				return false;

			TrackedDevicePose_t pose = {};
			dev->GetPose(trackingSpace, &pose, ETrackingStateType::TrackingStateType_Rendering);
			if (!pose.bPoseIsValid)
				return false;
			// S2G_m34 makes ordinary glm (column-first) matrices, so transpose the product to overlayTransform's layout
			glm::mat4 device = S2G_m34(pose.mDeviceToAbsoluteTracking);
			out = glm::transpose(component ? device : device * S2G_m34(transformData.deviceRelative.offset));
		}
		return true;
	}
};

// TODO don't pass around handles, as it will cause
// crashes when we should merely return VROverlayError_InvalidHandle
#define OVL (*((OverlayData**)pOverlayHandle))
#define USEH()                                                                        \
	OverlayData* overlay = (OverlayData*)ulOverlayHandle;                             \
	if (!overlay || !validOverlays.count(overlay) || !overlays.count(overlay->key)) { \
		return VROverlayError_InvalidHandle;                                          \
	}

#define USEHB()                                                                       \
	OverlayData* overlay = (OverlayData*)ulOverlayHandle;                             \
	if (!overlay || !validOverlays.count(overlay) || !overlays.count(overlay->key)) { \
		return false;                                                                 \
	}

// k_unVROverlayMaxKeyLength and k_unVROverlayMaxNameLength, the same in every SDK version: with the terminator
static constexpr uint32_t kOverlayMaxKeyLength = 128;
static constexpr uint32_t kOverlayMaxNameLength = 128;

// The overlay that has gamepad focus (SetGamepadFocusOverlay)
static VROverlayHandle_t gamepadFocusOverlay = k_ulOverlayHandleInvalid;

BaseOverlay::~BaseOverlay()
{
	for (const auto& kv : overlays) {
		if (kv.second) {
			delete kv.second;
		}
	}
}

// Overlays curved less than this are drawn flat: the bend is under a millimetre per metre of width, and the
// cylinder's radius grows so large that it loses precision
static constexpr float minOverlayCurvature = 1e-3f;

/**
 * Splits an overlay transform, stored like OverlayData::overlayTransform (rows first), into a quad's orientation and
 * the scale of its X and Y axes. A quad only uses its X and Y axes, so Z is made from them: that way every texel lands
 * where the transform puts it, mirrored or not (a mirrored overlay just faces the other way). Returns false if X or Y
 * is zero or they're parallel (a degenerate transform, such as one built from an untracked device's empty pose).
 */
static bool QuatAndScaleFromMatrix(glm::quat* orientation, const glm::mat4& mat, float* xscale, float* yscale)
{
	// The overlay's X and Y axes in the tracking space are the first two columns of the 3x3 part
	glm::vec3 x = glm::vec3(mat[0][0], mat[1][0], mat[2][0]);
	glm::vec3 y = glm::vec3(mat[0][1], mat[1][1], mat[2][1]);
	*xscale = glm::length(x);
	*yscale = glm::length(y);
	if (!(*xscale > 1e-6f) || !(*yscale > 1e-6f)) // also catches NaN
		return false;
	x /= *xscale;
	y /= *yscale;

	glm::vec3 z = glm::cross(x, y);
	const float zLength = glm::length(z);
	if (!(zLength > 1e-6f))
		return false;
	z /= zLength;
	y = glm::cross(z, x); // square off any shear

	// glm::mat3 takes columns
	*orientation = glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
	return true;
}

static bool IsFinite(const glm::vec3& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

int BaseOverlay::_BuildLayers(XrCompositionLayerBaseHeader* sceneLayer, XrCompositionLayerBaseHeader const* const*& layers)
{
	// Note that at least on MSVC, this shouldn't be doing any memory allocations
	//  unless the list is expanding from new layers.
	layerHeaders.clear();
	if (sceneLayer)
		layerHeaders.push_back(sceneLayer);

	// For performance reasons, this indicates that something is using input
	// Don't write it directly to usingInput, as that could cause race conditions between
	//  when it's initially set to false, and when it's reset to true.
	bool checkUsingInput = false;

	if (keyboard) {
#ifndef OC_XR_PORT
		layerHeaders.push_back(keyboard->Update());
#endif
		checkUsingInput = true;

		if (keyboard->IsClosed())
			HideKeyboard();
	}

	// sort overlays by distance starting with this index
	size_t firstOverlay = layerHeaders.size();
	// find HMD position to sort overlays by distance
	TrackedDevicePose_t hmdPose = {};
	vr::ETrackingUniverseOrigin trackingSpace = GetUnsafeBaseCompositor()->GetTrackingSpace();
	glm::vec3 hmdPosition = { 0.0f, 0.0f, 0.0f };
	std::shared_ptr<ITrackedDevice> hmd = BackendManager::Instance().GetDevice(vr::k_unTrackedDeviceIndex_Hmd);
	if (hmd) {
		hmd->GetPose(trackingSpace, &hmdPose, ETrackingStateType::TrackingStateType_Rendering);
		if (hmdPose.bPoseIsValid) {
			const auto& hmdM = hmdPose.mDeviceToAbsoluteTracking.m;
			hmdPosition = { hmdM[0][3], hmdM[1][3], hmdM[2][3] };
		}
	}
	layerOverlays.clear();

	for (const auto& kv : overlays) {
		if (kv.second) {
			OverlayData& overlay = *kv.second;

			// Skip hiddden overlays, and those without a valid texture (eg, after calling ClearOverlayTexture).
			if (!overlay.visible || overlay.texture.handle == nullptr)
				continue;

			// Quick hack to get around Boneworks creating overlays and setting them to an opacity of
			// zero to hide them. Leave 1% of margin in case of weird float issues.
			// if (overlay.colour.a < 0.01)
			//	continue;

			if ((uint64_t)overlay.compositionLayer.quad.subImage.swapchain == 0) {
				continue;
			}

			const XrRect2Di& srcSize = overlay.compositionLayer.quad.subImage.imageRect;
			// (a flipped texture is drawn with a negative height)
			const int32_t srcHeight = std::abs(srcSize.extent.height);
			if (srcHeight <= 8 && srcSize.extent.width <= 8) {
				// Hack for F1 22 which creates a low res texture to fade between scenes
				// but ends up just leaving a black square that takes up half the screen.
				continue;
			}

			// Skip overlay if we can't compute its transform
			glm::mat4 transform = glm::mat4(0.0f);
			if (!overlay.ComputeOverlayTransform(trackingSpace, transform)) {
				continue;
			}

			// Calculate the texture's aspect ratio
			const float aspect = srcHeight > 0 ? (float)srcSize.extent.width / (float)srcHeight : 1.0f;
			// ... and use that to set the size of the overlay, as it will appear to the user
			// Note we shouldn't do this when setting the texture, as the user may change the width of
			//  the overlay without changing the texture.
			float xscale, yscale;
			glm::quat orientation;
			const glm::vec3 position = { transform[0][3], transform[1][3], transform[2][3] };
			if (!QuatAndScaleFromMatrix(&orientation, transform, &xscale, &yscale) || !IsFinite(position))
				continue;

			const float width = overlay.widthMeters * xscale;
			const float height = overlay.widthMeters / aspect * yscale;
			// The transform is in the current tracking space, which the app may have changed since it set the texture
			overlay.compositionLayer.base.space = xr_space_from_ref_space_type(BaseSystem::currentSpace);

			const bool curved = overlay.curvature >= minOverlayCurvature && xr_ext->CylinderKHR_Available()
			    && std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0;
			if (curved) {
				// OpenVR's curvature is the overlay's width over the cylinder's circumference (1 is a closed cylinder).
				// An OpenXR cylinder's visible arc is centred on its -Z, so its centre is a radius in front of the overlay.
				const float curvature = std::min(overlay.curvature, 1.0f);
				const float radius = width / (glm::two_pi<float>() * curvature);
				const glm::vec3 centre = position + orientation * glm::vec3(0.0f, 0.0f, radius);
				overlay.compositionLayer.base.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
				overlay.compositionLayer.cylinder.radius = radius;
				// OpenXR wants it below 2π, which a float 2π rounds above
				overlay.compositionLayer.cylinder.centralAngle = std::min(glm::two_pi<float>() * curvature, std::nextafter(glm::two_pi<float>(), 0.0f));
				overlay.compositionLayer.cylinder.aspectRatio = width / height;
				overlay.compositionLayer.cylinder.pose = { G2X_quat(orientation), G2X_v3f(centre) };
			} else {
				overlay.compositionLayer.base.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
				overlay.compositionLayer.quad.size.width = width;
				overlay.compositionLayer.quad.size.height = height;
				overlay.compositionLayer.quad.pose = { G2X_quat(orientation), G2X_v3f(position) };
			}

			overlay.distance = glm::distance(position, hmdPosition);
			size_t i;

			// Sort the overlays: later layers are drawn on top, so by sort order, then the farthest first
			for (i = firstOverlay; i < layerHeaders.size(); i++) {
				const OverlayData* d = layerOverlays[i - firstOverlay];
				if (d->sortOrder > overlay.sortOrder)
					break;
				if (d->sortOrder == overlay.sortOrder && d->distance < overlay.distance)
					break;
			}
			// Finally, add it to the list of layers to be sent to LibOVR
			layerHeaders.insert(layerHeaders.begin() + i, &overlay.compositionLayer.base);
			layerOverlays.insert(layerOverlays.begin() + (i - firstOverlay), &overlay);
		}
	}

	usingInput = checkUsingInput;
	layers = layerHeaders.data();
	return static_cast<int>(layerHeaders.size());
}

bool BaseOverlay::_HandleOverlayInput(EVREye side, TrackedDeviceIndex_t index, VRControllerState_t state)
{
	if (!usingInput)
		return true;

	if (!keyboard)
		return true;

#ifndef OC_XR_PORT
	keyboard->HandleOverlayInput(side, state, (float)ovr_GetTimeInSeconds());
#endif
	return false;
}

EVROverlayError BaseOverlay::FindOverlay(const char* pchOverlayKey, VROverlayHandle_t* pOverlayHandle)
{
	if (!pchOverlayKey || !pOverlayHandle)
		return VROverlayError_InvalidParameter;
	*pOverlayHandle = k_ulOverlayHandleInvalid;
	if (overlays.count(pchOverlayKey)) {
		OVL = overlays[pchOverlayKey];
		return VROverlayError_None;
	}

	// TODO is this the correct return value
	return VROverlayError_InvalidParameter;
}
EVROverlayError BaseOverlay::CreateOverlay(const char* pchOverlayKey, const char* pchOverlayName, VROverlayHandle_t* pOverlayHandle)
{
	if (!pchOverlayKey || !pchOverlayName || !pOverlayHandle)
		return VROverlayError_InvalidParameter;
	*pOverlayHandle = k_ulOverlayHandleInvalid;
	// The SDK's limits, including the terminator
	if (strlen(pchOverlayKey) >= kOverlayMaxKeyLength)
		return VROverlayError_KeyTooLong;
	if (strlen(pchOverlayName) >= kOverlayMaxNameLength)
		return VROverlayError_NameTooLong;
	if (overlays.count(pchOverlayKey)) {
		return VROverlayError_KeyInUse;
	}

	OverlayData* data = new OverlayData(pchOverlayKey, pchOverlayName);
	OVL = data;

	overlays[pchOverlayKey] = data;
	validOverlays.insert(data);

	data->compositionLayer.base.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
	data->compositionLayer.base.next = NULL;
	data->compositionLayer.base.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	data->compositionLayer.base.space = xr_space_from_ref_space_type(BaseSystem::currentSpace);

	// Still same for quad + cylinder
	data->compositionLayer.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	data->compositionLayer.quad.pose = { { 0.f, 0.f, 0.f, 1.f },
		{ 0.0f, 0.0f, -0.65f } };

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::DestroyOverlay(VROverlayHandle_t ulOverlayHandle)
{
	USEH();

	if (highQualityOverlay == ulOverlayHandle)
		highQualityOverlay = vr::k_ulOverlayHandleInvalid;

	if (std::shared_ptr<Compositor> comp = overlay->compositor.lock()) {
		auto* backend = (XrBackend*)BackendManager::Instance().GetBackendInstance();
		backend->UnregisterOverlayCompositor(comp);
	}
	overlays.erase(overlay->key);
	validOverlays.erase(overlay);
	delete overlay;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetHighQualityOverlay(VROverlayHandle_t ulOverlayHandle)
{
	USEH();

	highQualityOverlay = ulOverlayHandle;

	return VROverlayError_None;
}
VROverlayHandle_t BaseOverlay::GetHighQualityOverlay()
{
	if (!highQualityOverlay)
		return k_ulOverlayHandleInvalid;

	return highQualityOverlay;
}
// Copies value into an app's buffer of size bytes, cut short if it doesn't fit and always terminated, and returns
// the size needed including the terminator (as SteamVR does)
static uint32_t CopyOutString(const std::string& value, char* buffer, uint32_t size)
{
	if (buffer && size) {
		size_t n = std::min<size_t>(value.size(), size - 1);
		memcpy(buffer, value.data(), n);
		buffer[n] = 0;
	}
	return (uint32_t)value.size() + 1;
}

uint32_t BaseOverlay::GetOverlayKey(VROverlayHandle_t ulOverlayHandle, char* pchValue, uint32_t unBufferSize, EVROverlayError* pError)
{
	OverlayData* overlay = (OverlayData*)ulOverlayHandle;
	if (!overlay || !validOverlays.count(overlay) || !overlays.count(overlay->key)) {
		if (pError)
			*pError = VROverlayError_InvalidHandle;
		CopyOutString("", pchValue, unBufferSize);
		return 0;
	}
	if (pError)
		*pError = VROverlayError_None;
	return CopyOutString(overlay->key, pchValue, unBufferSize);
}
uint32_t BaseOverlay::GetOverlayName(VROverlayHandle_t ulOverlayHandle, VR_OUT_STRING() char* pchValue, uint32_t unBufferSize, EVROverlayError* pError)
{
	OverlayData* overlay = (OverlayData*)ulOverlayHandle;
	if (!overlay || !validOverlays.count(overlay) || !overlays.count(overlay->key)) {
		if (pError)
			*pError = VROverlayError_InvalidHandle;
		CopyOutString("", pchValue, unBufferSize);
		return 0;
	}
	if (pError)
		*pError = VROverlayError_None;
	return CopyOutString(overlay->name, pchValue, unBufferSize);
}
EVROverlayError BaseOverlay::SetOverlayName(VROverlayHandle_t ulOverlayHandle, const char* pchName)
{
	USEH();
	if (!pchName)
		return VROverlayError_InvalidParameter;
	if (strlen(pchName) >= kOverlayMaxNameLength)
		return VROverlayError_NameTooLong;

	overlay->name = pchName;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayImageData(VROverlayHandle_t ulOverlayHandle, void* pvBuffer, uint32_t unBufferSize, uint32_t* punWidth, uint32_t* punHeight)
{
	USEH();
	if (punWidth)
		*punWidth = 0;
	if (punHeight)
		*punHeight = 0;
	OC_UNSUPPORTED(VROverlayError_RequestFailed, "GetOverlayImageData: overlay images can't be read back");
}
const char* BaseOverlay::GetOverlayErrorNameFromEnum(EVROverlayError error)
{
#define ERR_CASE(name)          \
	case VROverlayError_##name: \
		return #name;
	switch (error) {
		ERR_CASE(None);
		ERR_CASE(UnknownOverlay);
		ERR_CASE(InvalidHandle);
		ERR_CASE(PermissionDenied);
		ERR_CASE(OverlayLimitExceeded);
		ERR_CASE(WrongVisibilityType);
		ERR_CASE(KeyTooLong);
		ERR_CASE(NameTooLong);
		ERR_CASE(KeyInUse);
		ERR_CASE(WrongTransformType);
		ERR_CASE(InvalidTrackedDevice);
		ERR_CASE(InvalidParameter);
		ERR_CASE(ThumbnailCantBeDestroyed);
		ERR_CASE(ArrayTooSmall);
		ERR_CASE(RequestFailed);
		ERR_CASE(InvalidTexture);
		ERR_CASE(UnableToLoadFile);
		ERR_CASE(KeyboardAlreadyInUse);
		ERR_CASE(NoNeighbor);
		ERR_CASE(TooManyMaskPrimitives);
		ERR_CASE(BadMaskPrimitive);
		ERR_CASE(TextureAlreadyLocked);
		ERR_CASE(TextureLockCapacityReached);
		ERR_CASE(TextureNotLocked);
		ERR_CASE(TimedOut);
	}
#undef ERR_CASE

	const char* name = OC_EnumName("EVROverlayError", (int32_t)error);
	return name ? name : "Unknown";
}
EVROverlayError BaseOverlay::SetOverlayRenderingPid(VROverlayHandle_t ulOverlayHandle, uint32_t unPID)
{
	USEH();
	overlay->renderingPid = unPID;
	return VROverlayError_None;
}
uint32_t BaseOverlay::GetOverlayRenderingPid(VROverlayHandle_t ulOverlayHandle)
{
	OverlayData* overlay = (OverlayData*)ulOverlayHandle;
	if (!overlay || !validOverlays.count(overlay) || !overlays.count(overlay->key))
		return 0;
	return overlay->renderingPid;
}
EVROverlayError BaseOverlay::SetOverlayFlag(VROverlayHandle_t ulOverlayHandle, VROverlayFlags eOverlayFlag, bool bEnabled)
{
	USEH();

	if (bEnabled) {
		overlay->flags |= 1uLL << eOverlayFlag;
	} else {
		overlay->flags &= ~(1uLL << eOverlayFlag);
	}

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayFlag(VROverlayHandle_t ulOverlayHandle, VROverlayFlags eOverlayFlag, bool* pbEnabled)
{
	USEH();

	*pbEnabled = (overlay->flags & (1uLL << eOverlayFlag)) != 0uLL;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayColor(VROverlayHandle_t ulOverlayHandle, float fRed, float fGreen, float fBlue)
{
	USEH();

	overlay->colour.r = fRed;
	overlay->colour.g = fGreen;
	overlay->colour.b = fBlue;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayColor(VROverlayHandle_t ulOverlayHandle, float* pfRed, float* pfGreen, float* pfBlue)
{
	USEH();

	*pfRed = overlay->colour.r;
	*pfGreen = overlay->colour.g;
	*pfBlue = overlay->colour.b;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayAlpha(VROverlayHandle_t ulOverlayHandle, float fAlpha)
{
	USEH();

	overlay->colour.a = fAlpha;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayAlpha(VROverlayHandle_t ulOverlayHandle, float* pfAlpha)
{
	USEH();

	*pfAlpha = overlay->colour.a;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTexelAspect(VROverlayHandle_t ulOverlayHandle, float fTexelAspect)
{
	USEH();

	overlay->texelAspect = fTexelAspect;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTexelAspect(VROverlayHandle_t ulOverlayHandle, float* pfTexelAspect)
{
	USEH();

	if (!pfTexelAspect)
		return VROverlayError_InvalidParameter;

	*pfTexelAspect = overlay->texelAspect;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlaySortOrder(VROverlayHandle_t ulOverlayHandle, uint32_t unSortOrder)
{
	USEH();
	overlay->sortOrder = unSortOrder;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlaySortOrder(VROverlayHandle_t ulOverlayHandle, uint32_t* punSortOrder)
{
	USEH();
	*punSortOrder = overlay->sortOrder;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayWidthInMeters(VROverlayHandle_t ulOverlayHandle, float fWidthInMeters)
{
	USEH();

	overlay->widthMeters = fWidthInMeters;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayWidthInMeters(VROverlayHandle_t ulOverlayHandle, float* pfWidthInMeters)
{
	USEH();

	*pfWidthInMeters = overlay->widthMeters;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayCurvature(VROverlayHandle_t ulOverlayHandle, float fCurvature)
{
	USEH();

	// _BuildLayers draws it as a cylinder where it can
	overlay->curvature = fCurvature;
	if (fCurvature >= minOverlayCurvature && !xr_ext->CylinderKHR_Available())
		OOVR_LOG_ONCE("WARNING: Tried to set overlay curvature, but XR_KHR_composition_layer_cylinder is not available");

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayCurvature(VROverlayHandle_t ulOverlayHandle, float* pfCurvature)
{
	USEH();

	*pfCurvature = overlay->curvature;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayAutoCurveDistanceRangeInMeters(VROverlayHandle_t ulOverlayHandle, float fMinDistanceInMeters, float fMaxDistanceInMeters)
{
	USEH();

	overlay->autoCurveDistanceRangeMin = fMinDistanceInMeters;
	overlay->autoCurveDistanceRangeMax = fMaxDistanceInMeters;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayAutoCurveDistanceRangeInMeters(VROverlayHandle_t ulOverlayHandle, float* pfMinDistanceInMeters, float* pfMaxDistanceInMeters)
{
	USEH();

	*pfMinDistanceInMeters = overlay->autoCurveDistanceRangeMin;
	*pfMaxDistanceInMeters = overlay->autoCurveDistanceRangeMax;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTextureColorSpace(VROverlayHandle_t ulOverlayHandle, EColorSpace eTextureColorSpace)
{
	USEH();

	overlay->colourSpace = eTextureColorSpace;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTextureColorSpace(VROverlayHandle_t ulOverlayHandle, EColorSpace* peTextureColorSpace)
{
	USEH();

	*peTextureColorSpace = overlay->colourSpace;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTextureBounds(VROverlayHandle_t ulOverlayHandle, const VRTextureBounds_t* pOverlayTextureBounds)
{
	USEH();

	if (pOverlayTextureBounds)
		overlay->textureBounds = *pOverlayTextureBounds;
	else
		overlay->textureBounds = { 0, 0, 1, 1 };

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTextureBounds(VROverlayHandle_t ulOverlayHandle, VRTextureBounds_t* pOverlayTextureBounds)
{
	USEH();

	*pOverlayTextureBounds = overlay->textureBounds;

	return VROverlayError_None;
}
uint32_t BaseOverlay::GetOverlayRenderModel(VROverlayHandle_t ulOverlayHandle, char* pchValue, uint32_t unBufferSize, HmdColor_t* pColor, EVROverlayError* pError)
{
	auto fail = [&](EVROverlayError e) -> uint32_t {
		if (pError)
			*pError = e;
		if (pchValue && unBufferSize)
			pchValue[0] = 0;
		return 0;
	};
	OverlayData* overlay = (OverlayData*)ulOverlayHandle;
	if (!overlay || !validOverlays.count(overlay) || !overlays.count(overlay->key))
		return fail(VROverlayError_InvalidHandle);
	if (pError)
		*pError = VROverlayError_None;
	if (pColor)
		*pColor = overlay->renderModelColor;
	uint32_t needed = (uint32_t)overlay->renderModel.size() + 1;
	if (pchValue && unBufferSize >= needed)
		memcpy(pchValue, overlay->renderModel.c_str(), needed);
	else if (pchValue && unBufferSize)
		pchValue[0] = 0;
	return needed;
}
EVROverlayError BaseOverlay::SetOverlayRenderModel(VROverlayHandle_t ulOverlayHandle, const char* pchRenderModel, const HmdColor_t* pColor)
{
	USEH();
	overlay->renderModel = pchRenderModel ? pchRenderModel : "";
	if (pColor)
		overlay->renderModelColor = *pColor;
	OOVR_LOG_ONCE("Overlay render models are kept but not drawn");
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTransformType(VROverlayHandle_t ulOverlayHandle, VROverlayTransformType* peTransformType)
{
	USEH();

	*peTransformType = overlay->transformType;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTransformAbsolute(VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin eTrackingOrigin, const HmdMatrix34_t* pmatTrackingOriginToOverlayTransform)
{
	USEH();

	// TODO account for the universe origin, and if it doesn't match that currently in use then add or
	//  subtract the floor position to match it. This shouldn't usually be an issue though, as I can't
	//  imagine many apps will use a different origin for their overlays.

	overlay->transformType = VROverlayTransform_Absolute;
	S2O_om44(*pmatTrackingOriginToOverlayTransform, overlay->overlayTransform);

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTransformAbsolute(VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin* peTrackingOrigin, HmdMatrix34_t* pmatTrackingOriginToOverlayTransform)
{
	USEH();

	if (overlay->transformType != VROverlayTransform_Absolute)
		return VROverlayError_WrongTransformType;

	O2S_om34(overlay->overlayTransform, *pmatTrackingOriginToOverlayTransform);

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTransformTrackedDeviceRelative(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unTrackedDevice, const HmdMatrix34_t* pmatTrackedDeviceToOverlayTransform)
{
	USEH();

	overlay->transformType = VROverlayTransform_TrackedDeviceRelative;
	overlay->transformData.deviceRelative.device = unTrackedDevice;
	overlay->transformData.deviceRelative.offset = *pmatTrackedDeviceToOverlayTransform;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTransformTrackedDeviceRelative(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t* punTrackedDevice, HmdMatrix34_t* pmatTrackedDeviceToOverlayTransform)
{
	USEH();
	if (!punTrackedDevice || !pmatTrackedDeviceToOverlayTransform)
		return VROverlayError_InvalidParameter;
	if (overlay->transformType != VROverlayTransform_TrackedDeviceRelative)
		return VROverlayError_WrongTransformType;
	*punTrackedDevice = overlay->transformData.deviceRelative.device;
	*pmatTrackedDeviceToOverlayTransform = overlay->transformData.deviceRelative.offset;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTransformTrackedDeviceComponent(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unDeviceIndex, const char* pchComponentName)
{
	USEH();
	overlay->transformType = VROverlayTransform_TrackedComponent;
	overlay->componentDevice = unDeviceIndex;
	overlay->componentName = pchComponentName ? pchComponentName : "";
	OOVR_LOG_ONCE("Overlays on a tracked device's component are placed at the device itself");
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTransformTrackedDeviceComponent(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t* punDeviceIndex, char* pchComponentName, uint32_t unComponentNameSize)
{
	USEH();
	if (overlay->transformType != VROverlayTransform_TrackedComponent)
		return VROverlayError_WrongTransformType;
	if (punDeviceIndex)
		*punDeviceIndex = overlay->componentDevice;
	if (pchComponentName && unComponentNameSize) {
		if (overlay->componentName.size() + 1 > unComponentNameSize) {
			pchComponentName[0] = 0;
			return VROverlayError_ArrayTooSmall;
		}
		memcpy(pchComponentName, overlay->componentName.c_str(), overlay->componentName.size() + 1);
	}
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTransformOverlayRelative(VROverlayHandle_t ulOverlayHandle, VROverlayHandle_t* ulOverlayHandleParent, HmdMatrix34_t* pmatParentOverlayToOverlayTransform)
{
	USEH();
	if (!ulOverlayHandleParent || !pmatParentOverlayToOverlayTransform)
		return VROverlayError_InvalidParameter;
	*ulOverlayHandleParent = overlay->relativeParent;
	*pmatParentOverlayToOverlayTransform = overlay->relativeTransform;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTransformOverlayRelative(VROverlayHandle_t ulOverlayHandle, VROverlayHandle_t ulOverlayHandleParent, const HmdMatrix34_t* pmatParentOverlayToOverlayTransform)
{
	USEH();
	overlay->relativeParent = ulOverlayHandleParent;
	if (pmatParentOverlayToOverlayTransform)
		overlay->relativeTransform = *pmatParentOverlayToOverlayTransform;
	OOVR_LOG_ONCE("Overlay-relative transforms are kept but not placed yet");
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTransformCursor(VROverlayHandle_t ulCursorOverlayHandle, const HmdVector2_t* pvHotspot)
{
	OverlayData* overlay = (OverlayData*)ulCursorOverlayHandle;
	if (!overlay || !validOverlays.count(overlay) || !overlays.count(overlay->key))
		return VROverlayError_InvalidHandle;
	if (pvHotspot)
		overlay->cursorHotspot = *pvHotspot;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayTransformCursor(VROverlayHandle_t ulOverlayHandle, HmdVector2_t* pvHotspot)
{
	USEH();
	if (!pvHotspot)
		return VROverlayError_InvalidParameter;
	*pvHotspot = overlay->cursorHotspot;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTransformProjection(VROverlayHandle_t ulOverlayHandle,
    ETrackingUniverseOrigin eTrackingOrigin, const HmdMatrix34_t* pmatTrackingOriginToOverlayTransform,
    const OOVR_VROverlayProjection_t* pProjection, EVREye eEye)
{
	USEH();
	if (pmatTrackingOriginToOverlayTransform) {
		overlay->transformType = VROverlayTransform_Absolute;
		S2O_om44(*pmatTrackingOriginToOverlayTransform, overlay->overlayTransform);
	}
	OOVR_LOG_ONCE("Projection overlays are drawn as absolute ones");
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::ShowOverlay(VROverlayHandle_t ulOverlayHandle)
{
	USEH();
	overlay->visible = true;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::HideOverlay(VROverlayHandle_t ulOverlayHandle)
{
	USEH();
	overlay->visible = false;
	return VROverlayError_None;
}
bool BaseOverlay::IsOverlayVisible(VROverlayHandle_t ulOverlayHandle)
{
	USEHB();
	return overlay->visible;
}
EVROverlayError BaseOverlay::GetTransformForOverlayCoordinates(VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin eTrackingOrigin, HmdVector2_t coordinatesInOverlay, HmdMatrix34_t* pmatTransform)
{
	USEH();
	if (!pmatTransform)
		return VROverlayError_InvalidParameter;
	// The overlay's own transform: the offset within it isn't added yet
	O2S_om34(overlay->overlayTransform, *pmatTransform);
	OC_UNSUPPORTED(VROverlayError_None, "GetTransformForOverlayCoordinates: answered with the overlay's own transform");
}
bool BaseOverlay::PollNextOverlayEvent(VROverlayHandle_t ulOverlayHandle, VREvent_t* pEvent, uint32_t eventSize)
{
	USEHB();

	memset(pEvent, 0, eventSize);

	if (overlay->eventQueue.empty())
		return false;

	VREvent_t e = overlay->eventQueue.front();
	overlay->eventQueue.pop();

	memcpy(pEvent, &e, std::min((uint32_t)sizeof(e), eventSize));

	return true;
}
EVROverlayError BaseOverlay::GetOverlayInputMethod(VROverlayHandle_t ulOverlayHandle, VROverlayInputMethod* peInputMethod)
{
	USEH();

	if (peInputMethod)
		*peInputMethod = overlay->inputMethod;

	return VROverlayError_None;
}

EVROverlayError BaseOverlay::SetOverlayInputMethod(VROverlayHandle_t ulOverlayHandle, VROverlayInputMethod eInputMethod)
{
	USEH();

	overlay->inputMethod = eInputMethod;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayMouseScale(VROverlayHandle_t ulOverlayHandle, HmdVector2_t* pvecMouseScale)
{
	USEH();

	*pvecMouseScale = overlay->mouseScale;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayMouseScale(VROverlayHandle_t ulOverlayHandle, const HmdVector2_t* pvecMouseScale)
{
	USEH();

	if (pvecMouseScale)
		overlay->mouseScale = *pvecMouseScale;
	else
		overlay->mouseScale = HmdVector2_t{ 1.0f, 1.0f };

	return VROverlayError_None;
}
bool BaseOverlay::ComputeOverlayIntersection(VROverlayHandle_t ulOverlayHandle, const OOVR_VROverlayIntersectionParams_t* pParams, OOVR_VROverlayIntersectionResults_t* pResults)
{
	USEHB();

	// first, get transform
	glm::mat4 transform = glm::mat4(0.0f);
	if(!overlay->ComputeOverlayTransform(pParams->eOrigin, transform))
		return false;
	// The overlay's axes, with Z made from X and Y as for drawing it (see QuatAndScaleFromMatrix)
	const glm::vec3 xAxis = glm::vec3(transform[0][0], transform[1][0], transform[2][0]);
	const glm::vec3 yAxis = glm::vec3(transform[0][1], transform[1][1], transform[2][1]);
	const glm::vec3 position = glm::vec3(transform[0][3], transform[1][3], transform[2][3]);
	glm::quat orientation;
	float xscale, yscale;
	if (!QuatAndScaleFromMatrix(&orientation, transform, &xscale, &yscale) || !IsFinite(position))
		return false; // not drawn either
	const glm::vec3 zAxis = orientation * glm::vec3(0.0f, 0.0f, 1.0f);
	// (glm::mat3 takes columns; the maths below multiplies row vectors by the basis, so it wants the transpose)
	glm::mat3 basis = glm::transpose(glm::mat3(xAxis, yAxis, zAxis));
	// world space -> overlay space
	glm::mat3 invertedBasis = glm::inverse(basis);

	// collect the data
	auto& s = pParams->vSource.v;
	glm::vec3 src = { s[0], s[1], s[2] };
	auto& d = pParams->vDirection.v;
	glm::vec3 dir = { d[0], d[1], d[2] };

	// move src to overlay space
	glm::vec3 src_proj = (src - position) * invertedBasis;
	if (src_proj.z < 0)
		return false;
	glm::vec3 dir_proj = dir * invertedBasis;

	// only intersect with forward ray, not one nearly parallel to the overlay (its angle measured in the world, as
	// the overlay's scale would skew it in overlay space; zAxis is a unit normal, so this is also dir_proj.z)
	if (!(glm::dot(dir, zAxis) < -0.01f * glm::length(dir)))
		return false;

	// assign z to zero to fing intersection
	glm::vec3 dst_proj = src_proj - dir_proj * (src_proj.z / dir_proj.z);

	// convert it back
	glm::vec3 dst = dst_proj * basis + position;
	glm::vec3 n = glm::normalize(glm::vec3{ 0.0f, 0.0f, 1.0f } * basis);

	// build result
	pResults->vNormal = { n[0], n[1], n[2] };
	pResults->vPoint = { dst[0], dst[1], dst[2] };
	// clang-format off
	// The overlay is widthMeters wide, and as tall as its texture's aspect ratio makes it
	const XrExtent2Di& extent = overlay->compositionLayer.quad.subImage.imageRect.extent;
	const float heightMeters = extent.width > 0 && extent.height != 0 ? overlay->widthMeters * std::abs(extent.height) / extent.width : overlay->widthMeters;
	pResults->vUVs = { dst_proj.x / (float)overlay->widthMeters + 0.5f,
	                   dst_proj.y / heightMeters + 0.5f };
	// clang-format on
	pResults->fDistance = glm::length(dst - src);
	return pResults->vUVs.v[0] > 0 && pResults->vUVs.v[0] < 1 && pResults->vUVs.v[1] > 0 && pResults->vUVs.v[1] < 1;
}
bool BaseOverlay::HandleControllerOverlayInteractionAsMouse(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unControllerDeviceIndex)
{
	OC_UNSUPPORTED(false, "HandleControllerOverlayInteractionAsMouse: no laser mouse yet");
}
bool BaseOverlay::IsHoverTargetOverlay(VROverlayHandle_t ulOverlayHandle)
{
	OC_UNSUPPORTED(false, "IsHoverTargetOverlay: no laser mouse yet");
}
VROverlayHandle_t BaseOverlay::GetGamepadFocusOverlay()
{
	return gamepadFocusOverlay;
}
EVROverlayError BaseOverlay::SetGamepadFocusOverlay(VROverlayHandle_t ulNewFocusOverlay)
{
	if (ulNewFocusOverlay != k_ulOverlayHandleInvalid) {
		OverlayData* overlay = (OverlayData*)ulNewFocusOverlay;
		if (!validOverlays.count(overlay))
			return VROverlayError_InvalidHandle;
	}
	gamepadFocusOverlay = ulNewFocusOverlay;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayNeighbor(EOverlayDirection eDirection, VROverlayHandle_t ulFrom, VROverlayHandle_t ulTo)
{
	OverlayData* overlay = (OverlayData*)ulFrom;
	if (!overlay || !validOverlays.count(overlay))
		return VROverlayError_InvalidHandle;
	if (ulTo == k_ulOverlayHandleInvalid)
		overlay->neighbours.erase((int)eDirection);
	else
		overlay->neighbours[(int)eDirection] = ulTo;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::MoveGamepadFocusToNeighbor(EOverlayDirection eDirection, VROverlayHandle_t ulFrom)
{
	OverlayData* overlay = (OverlayData*)ulFrom;
	if (!overlay || !validOverlays.count(overlay))
		return VROverlayError_InvalidHandle;
	auto it = overlay->neighbours.find((int)eDirection);
	if (it == overlay->neighbours.end())
		return VROverlayError_NoNeighbor;
	gamepadFocusOverlay = it->second;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayDualAnalogTransform(VROverlayHandle_t ulOverlay, EDualAnalogWhich eWhich, const HmdVector2_t& vCenter, float fRadius)
{
	OverlayData* overlay = (OverlayData*)ulOverlay;
	if (!overlay || !validOverlays.count(overlay))
		return VROverlayError_InvalidHandle;
	overlay->dualAnalog[(int)eWhich] = { vCenter, fRadius };
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayDualAnalogTransform(VROverlayHandle_t ulOverlay, EDualAnalogWhich eWhich, HmdVector2_t* pvCenter, float* pfRadius)
{
	OverlayData* overlay = (OverlayData*)ulOverlay;
	if (!overlay || !validOverlays.count(overlay))
		return VROverlayError_InvalidHandle;
	auto it = overlay->dualAnalog.find((int)eWhich);
	if (pvCenter)
		*pvCenter = it == overlay->dualAnalog.end() ? HmdVector2_t{ 0, 0 } : it->second.first;
	if (pfRadius)
		*pfRadius = it == overlay->dualAnalog.end() ? 0.0f : it->second.second;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayDualAnalogTransform(VROverlayHandle_t ulOverlay, EDualAnalogWhich eWhich, const HmdVector2_t* pvCenter, float fRadius)
{
	if (!pvCenter)
		return VROverlayError_InvalidParameter;
	return SetOverlayDualAnalogTransform(ulOverlay, eWhich, *pvCenter, fRadius);
}
EVROverlayError BaseOverlay::TriggerLaserMouseHapticVibration(VROverlayHandle_t ulOverlayHandle, float fDurationSeconds, float fFrequency, float fAmplitude)
{
	USEH();
	OC_UNSUPPORTED(VROverlayError_None, "TriggerLaserMouseHapticVibration: no laser mouse yet");
}
EVROverlayError BaseOverlay::SetOverlayCursor(VROverlayHandle_t ulOverlayHandle, VROverlayHandle_t ulCursorHandle)
{
	USEH();
	overlay->cursor = ulCursorHandle;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayCursorPositionOverride(VROverlayHandle_t ulOverlayHandle, const HmdVector2_t* pvCursor)
{
	USEH();
	if (!pvCursor)
		return VROverlayError_InvalidParameter;
	overlay->cursorOverridden = true;
	overlay->cursorOverride = *pvCursor;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::ClearOverlayCursorPositionOverride(VROverlayHandle_t ulOverlayHandle)
{
	USEH();
	overlay->cursorOverridden = false;
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTexture(VROverlayHandle_t ulOverlayHandle, const Texture_t* pTexture)
{
	USEH();
	if (!pTexture)
		return VROverlayError_InvalidTexture;
	overlay->texture = *pTexture;

	BackendManager::Instance().OnOverlayTexture(&overlay->texture);

	if (!BackendManager::Instance().IsGraphicsConfigured())
		return VROverlayError_None;

	std::shared_ptr<Compositor> compositor = overlay->compositor.lock();
	if (!compositor) {
		// Overlay apps needn't have asked for IVRCompositor
		compositor = BaseCompositor::CreateCompositorAPI(&overlay->texture);
		// No compositor for this texture type: nothing to draw
		if (!compositor)
			OC_UNSUPPORTED(VROverlayError_InvalidTexture, "SetOverlayTexture: no compositor for texture type %d", (int)overlay->texture.eType);
		overlay->compositor = std::weak_ptr(compositor);
		auto* backend = (XrBackend*)BackendManager::Instance().GetBackendInstance();
		backend->RegisterOverlayCompositor(compositor);
	}

	compositor->LoadSubmitContext();
	// (subImage is the same for quad and cylinder layers)
	compositor->Invoke(&overlay->texture, &overlay->textureBounds, overlay->compositionLayer.quad.subImage);
	compositor->ResetSubmitContext();

	overlay->compositionLayer.base.space = xr_space_from_ref_space_type(BaseSystem::currentSpace);

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::ClearOverlayTexture(VROverlayHandle_t ulOverlayHandle)
{
	USEH();
	overlay->texture = {};

	if (std::shared_ptr<Compositor> comp = overlay->compositor.lock()) {
		auto* backend = (XrBackend*)BackendManager::Instance().GetBackendInstance();
		backend->UnregisterOverlayCompositor(comp);
	}
	overlay->compositor.reset();
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayRaw(VROverlayHandle_t ulOverlayHandle, void* pvBuffer, uint32_t unWidth, uint32_t unHeight, uint32_t unDepth)
{
	USEH();
	OC_UNSUPPORTED(VROverlayError_RequestFailed, "SetOverlayRaw: CPU images on overlays aren't supported yet");
}
EVROverlayError BaseOverlay::SetOverlayFromFile(VROverlayHandle_t ulOverlayHandle, const char* pchFilePath)
{
	USEH();
	OC_UNSUPPORTED(VROverlayError_RequestFailed, "SetOverlayFromFile: image files on overlays aren't supported yet");
}
EVROverlayError BaseOverlay::GetOverlayTexture(VROverlayHandle_t ulOverlayHandle, void** pNativeTextureHandle, void* pNativeTextureRef, uint32_t* pWidth, uint32_t* pHeight, uint32_t* pNativeFormat, ETextureType* pAPIType, EColorSpace* pColorSpace, VRTextureBounds_t* pTextureBounds)
{
	USEH();
	if (pNativeTextureHandle)
		*pNativeTextureHandle = nullptr;
	if (pWidth)
		*pWidth = 0;
	if (pHeight)
		*pHeight = 0;
	if (pNativeFormat)
		*pNativeFormat = 0;
	OC_UNSUPPORTED(VROverlayError_RequestFailed, "GetOverlayTexture: overlay textures can't be read back");
}
EVROverlayError BaseOverlay::ReleaseNativeOverlayHandle(VROverlayHandle_t ulOverlayHandle, void* pNativeTextureHandle)
{
	USEH();
	OC_UNSUPPORTED(VROverlayError_RequestFailed, "ReleaseNativeOverlayHandle: GetOverlayTexture hands out no textures");
}
EVROverlayError BaseOverlay::GetOverlayTextureSize(VROverlayHandle_t ulOverlayHandle, uint32_t* pWidth, uint32_t* pHeight)
{
	USEH();
	if (pWidth)
		*pWidth = 0;
	if (pHeight)
		*pHeight = 0;
	OC_UNSUPPORTED(VROverlayError_RequestFailed, "GetOverlayTextureSize: not known yet");
}
EVROverlayError BaseOverlay::CreateDashboardOverlay(const char* pchOverlayKey, const char* pchOverlayFriendlyName, VROverlayHandle_t* pMainHandle, VROverlayHandle_t* pThumbnailHandle)
{
	if (pMainHandle)
		*pMainHandle = k_ulOverlayHandleInvalid;
	if (pThumbnailHandle)
		*pThumbnailHandle = k_ulOverlayHandleInvalid;
	OC_UNSUPPORTED(VROverlayError_RequestFailed, "CreateDashboardOverlay: there's no dashboard yet");
}
bool BaseOverlay::IsDashboardVisible()
{
	// TODO should this be based of whether Dash is open?
	// Probably, but handling focus opens some other issues as it triggers under other conditions.
	return false;
}
bool BaseOverlay::IsActiveDashboardOverlay(VROverlayHandle_t ulOverlayHandle)
{
	OC_UNSUPPORTED(false, "IsActiveDashboardOverlay: there's no dashboard yet");
}
EVROverlayError BaseOverlay::SetDashboardOverlaySceneProcess(VROverlayHandle_t ulOverlayHandle, uint32_t unProcessId)
{
	USEH();
	OC_UNSUPPORTED(VROverlayError_None, "SetDashboardOverlaySceneProcess: there's no dashboard yet");
}
EVROverlayError BaseOverlay::GetDashboardOverlaySceneProcess(VROverlayHandle_t ulOverlayHandle, uint32_t* punProcessId)
{
	USEH();
	if (punProcessId)
		*punProcessId = 0;
	OC_UNSUPPORTED(VROverlayError_RequestFailed, "GetDashboardOverlaySceneProcess: there's no dashboard yet");
}
void BaseOverlay::ShowDashboard(const char* pchOverlayToShow)
{
	OC_UNSUPPORTED_VOID("ShowDashboard: there's no dashboard yet");
}
TrackedDeviceIndex_t BaseOverlay::GetPrimaryDashboardDevice()
{
	OC_UNSUPPORTED(k_unTrackedDeviceIndexInvalid, "GetPrimaryDashboardDevice: there's no dashboard yet");
}
EVROverlayError BaseOverlay::ShowKeyboardWithDispatch(EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode,
    const char* pchDescription, uint32_t unCharMax, const char* pchExistingText, bool bUseMinimalMode, uint64_t uUserValue,
    VRKeyboard::eventDispatch_t eventDispatch)
{
	// Submit a KeyboardDone event since the actual keyboard is not implemented yet.  This allows certain games to still proceed instead of crash.
	SubmitPlaceholderKeyboardEvent(VREvent_KeyboardDone, eventDispatch, uUserValue);

#ifndef OC_XR_PORT
	if (!BaseCompositor::dxcomp)
		return VROverlayError_RequestFailed;

	if (eLineInputMode != k_EGamepadTextInputLineModeSingleLine)
		return VROverlayError_RequestFailed;

	// TODO use description
	keyboard = make_unique<VRKeyboard>(BaseCompositor::dxcomp->GetDevice(), uUserValue, unCharMax, bUseMinimalMode, eventDispatch,
	    (VRKeyboard::EGamepadTextInputMode)eInputMode);

	keyboard->contents(VRKeyboard::CHAR_CONV.from_bytes(pchExistingText));

	BaseSystem* system = GetUnsafeBaseSystem();
	if (system) {
		system->_BlockInputsUntilReleased();
	}
#endif

	return VROverlayError_None;
}

/** Placeholder method for submitting a KeyboardDone event when asked to show the keyboard since it is not implemented yet. **/
void BaseOverlay::SubmitPlaceholderKeyboardEvent(vr::EVREventType ev, VRKeyboard::eventDispatch_t eventDispatch, uint64_t userValue)
{
	VREvent_Keyboard_t data{};
	data.uUserValue = userValue;

	VREvent_t evt = { 0 };
	evt.eventType = ev;
	evt.trackedDeviceIndex = 0;
	evt.data.keyboard = data;

	eventDispatch(evt);
}

EVROverlayError BaseOverlay::ShowKeyboard(EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode,
    const char* pchDescription, uint32_t unCharMax, const char* pchExistingText, bool bUseMinimalMode, uint64_t uUserValue)
{

	VRKeyboard::eventDispatch_t dispatch = [](VREvent_t ev) {
		BaseSystem* sys = GetUnsafeBaseSystem();
		if (sys) {
			sys->_EnqueueEvent(ev);
		}
	};

	return ShowKeyboardWithDispatch(eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText, bUseMinimalMode, uUserValue, dispatch);
}
EVROverlayError BaseOverlay::ShowKeyboard(EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode, uint32_t unFlags,
    const char* pchDescription, uint32_t unCharMax, const char* pchExistingText, uint64_t uUserValue)
{
	// KeyboardFlag_Minimal = 1 (EKeyboardFlags)
	return ShowKeyboard(eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText, (unFlags & 1) != 0, uUserValue);
}
EVROverlayError BaseOverlay::ShowKeyboardForOverlay(VROverlayHandle_t ulOverlayHandle,
    EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode,
    const char* pchDescription, uint32_t unCharMax, const char* pchExistingText,
    bool bUseMinimalMode, uint64_t uUserValue)
{

	USEH();

	VRKeyboard::eventDispatch_t dispatch = [overlay](VREvent_t ev) {
		overlay->eventQueue.push(ev);
	};

	return ShowKeyboardWithDispatch(eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText, bUseMinimalMode, uUserValue, dispatch);
}
EVROverlayError BaseOverlay::ShowKeyboardForOverlay(VROverlayHandle_t ulOverlayHandle, EGamepadTextInputMode eInputMode,
    EGamepadTextInputLineMode eLineInputMode, uint32_t unFlags, const char* pchDescription, uint32_t unCharMax,
    const char* pchExistingText, uint64_t uUserValue)
{
	// KeyboardFlag_Minimal = 1 (EKeyboardFlags)
	return ShowKeyboardForOverlay(ulOverlayHandle, eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText, (unFlags & 1) != 0, uUserValue);
}
uint32_t BaseOverlay::GetKeyboardText(char* pchText, uint32_t cchText)
{
	string str = keyboard ? VRKeyboard::CHAR_CONV.to_bytes(keyboard->contents()) : keyboardCache;

	// Since keyboard is not functional yet, return this default text (Adventurer because this fix was made specifically for Skyrim VR)
	str = "Adventurer";

	if (!pchText || cchText == 0)
		return (uint32_t)str.size() + 1;

	// Within cchText, cut short and terminated if it doesn't fit; returns the length copied, without the terminator
	CopyOutString(str, pchText, cchText);
	return (uint32_t)strlen(pchText);
}
void BaseOverlay::HideKeyboard()
{
	// First, if the keyboard is currently open, cache it's contents
	if (keyboard) {
		keyboardCache = VRKeyboard::CHAR_CONV.to_bytes(keyboard->contents());
	}

	// Delete the keyboard instance
	keyboard.reset();

	BaseSystem* system = GetUnsafeBaseSystem();
	if (system) {
		system->_BlockInputsUntilReleased();
	}
}
void BaseOverlay::SetKeyboardTransformAbsolute(ETrackingUniverseOrigin eTrackingOrigin, const HmdMatrix34_t* pmatTrackingOriginToKeyboardTransform)
{
	if (!keyboard || !pmatTrackingOriginToKeyboardTransform) {
		OOVR_LOG_ONCE("SetKeyboardTransformAbsolute with no keyboard open: ignored");
		return;
	}

#ifdef OC_XR_PORT
	OC_UNSUPPORTED_VOID("SetKeyboardTransformAbsolute: the keyboard isn't drawn yet");
#else
	BaseCompositor* compositor = GetUnsafeBaseCompositor();
	if (compositor && eTrackingOrigin != compositor->GetTrackingSpace()) {
		OC_UNSUPPORTED_VOID("SetKeyboardTransformAbsolute: origin %d isn't the compositor's %d", (int)eTrackingOrigin, (int)compositor->GetTrackingSpace());
	}

	keyboard->SetTransform(*pmatTrackingOriginToKeyboardTransform);
#endif
}
void BaseOverlay::SetKeyboardPositionForOverlay(VROverlayHandle_t ulOverlayHandle, HmdRect2_t avoidRect)
{
	OC_UNSUPPORTED_VOID("SetKeyboardPositionForOverlay: the keyboard isn't drawn yet");
}
EVROverlayError BaseOverlay::SetOverlayIntersectionMask(VROverlayHandle_t ulOverlayHandle, OOVR_VROverlayIntersectionMaskPrimitive_t* pMaskPrimitives, uint32_t unNumMaskPrimitives, uint32_t unPrimitiveSize)
{
	USEH();
	OC_UNSUPPORTED(VROverlayError_None, "SetOverlayIntersectionMask: kept as the whole overlay");
}
EVROverlayError BaseOverlay::GetOverlayFlags(VROverlayHandle_t ulOverlayHandle, uint32_t* pFlags)
{
	USEH();
	if (!pFlags)
		return VROverlayError_InvalidParameter;
	*pFlags = (uint32_t)overlay->flags;
	return VROverlayError_None;
}
BaseOverlay::VRMessageOverlayResponse BaseOverlay::ShowMessageOverlay(const char* pchText, const char* pchCaption, const char* pchButton0Text, const char* pchButton1Text, const char* pchButton2Text, const char* pchButton3Text)
{
	OC_UNSUPPORTED(VRMessageOverlayResponse_CouldntFindSystemOverlay, "ShowMessageOverlay: there are no message overlays yet");
}
void BaseOverlay::CloseMessageOverlay()
{
	OC_UNSUPPORTED_VOID("CloseMessageOverlay: there are no message overlays yet");
}

EVROverlayError BaseOverlay::SetOverlayPreCurvePitch(vr::VROverlayHandle_t ulOverlayHandle, float fRadians)
{
	USEH();
	overlay->preCurvePitch = fRadians;
	return VROverlayError_None;
}

EVROverlayError BaseOverlay::GetOverlayPreCurvePitch(vr::VROverlayHandle_t ulOverlayHandle, float* pfRadians)
{
	USEH();
	if (!pfRadians)
		return VROverlayError_InvalidParameter;
	*pfRadians = overlay->preCurvePitch;
	return VROverlayError_None;
}

EVROverlayError BaseOverlay::WaitFrameSync(uint32_t nTimeoutMs)
{
	return VROverlayError_None;
}

EVROverlayError BaseOverlay::CreateSubviewOverlay(VROverlayHandle_t parentOverlayHandle, const char* pchSubviewOverlayKey,
    const char* pchSubviewOverlayName, VROverlayHandle_t* pSubviewOverlayHandle)
{
	if (pSubviewOverlayHandle)
		*pSubviewOverlayHandle = k_ulOverlayHandleInvalid;
	return VROverlayError_RequestFailed;
}

EVROverlayError BaseOverlay::SetSubviewPosition(VROverlayHandle_t ulOverlayHandle, float fX, float fY)
{
	return VROverlayError_RequestFailed;
}
