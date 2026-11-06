#include "stdafx.h"
#include <cfloat>
#include <cmath>
#include <openxr/openxr.h>
#define BASE_IMPL
#include "../../DrvOpenXR/XrBackend.h"
#include "BaseCompositor.h"
#include "BaseOverlay.h"
#include "BaseSystem.h"
#include "Compositor/compositor.h"
#include "Drivers/Backend.h"
#include "Misc/Config.h"
#include "Misc/ScopeGuard.h"
#include "convert.h"
#include "generated/static_bases.gen.h"
#include <string>

using glm::mat4;
using glm::quat;
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
	}

	// The overlay's transform in the tracking space, stored like overlayTransform (rows first: translation in [i][3])
	bool ComputeOverlayTransform(vr::ETrackingUniverseOrigin trackingSpace, glm::mat4& out)
	{
		out = overlayTransform;
		if (transformType == VROverlayTransform_TrackedDeviceRelative) {
			std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDevice(transformData.deviceRelative.device);
			if (!dev)
				return false;

			TrackedDevicePose_t pose = {};
			dev->GetPose(trackingSpace, &pose, ETrackingStateType::TrackingStateType_Rendering);
			if (!pose.bPoseIsValid)
				return false;
			// S2G_m34 makes ordinary glm (column-first) matrices, so transpose the product to overlayTransform's layout
			out = glm::transpose(S2G_m34(pose.mDeviceToAbsoluteTracking) * S2G_m34(transformData.deviceRelative.offset));
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

#define USEHB()                                           \
	OverlayData* overlay = (OverlayData*)ulOverlayHandle; \
	if (!overlay || !overlays.count(overlay->key)) {      \
		return false;                                     \
	}

BaseOverlay::~BaseOverlay()
{
	for (const auto& kv : overlays) {
		if (kv.second) {
			delete kv.second;
		}
	}
}

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

			// Skip overlay if we can't compute it's transform
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

			if (overlay.compositionLayer.base.type == XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR) {
				const float scale_x = glm::length(glm::vec3(transform[0][0], transform[1][0], transform[2][0]));
				const float radius = scale_x / (2.0 * M_PI * overlay.curvature);
				const float angle = 2.0 * (scale_x / (2.0 * radius));

				const glm::vec3 center_point = position + glm::conjugate(orientation) * glm::vec3(0.0, 0.0, 1.0) * orientation * radius;

				overlay.compositionLayer.cylinder.radius = radius;
				overlay.compositionLayer.cylinder.centralAngle = angle;
				overlay.compositionLayer.cylinder.aspectRatio = (float)srcSize.extent.height / (float)srcSize.extent.width;
				overlay.compositionLayer.cylinder.pose = { { orientation.x, orientation.y, orientation.z, orientation.w },
					{ center_point.x, center_point.y, center_point.z } };
			} else {
				overlay.compositionLayer.quad.pose.position = G2X_v3f(position);
				overlay.compositionLayer.quad.pose.orientation = { orientation.x, orientation.y, orientation.z, orientation.w };
				overlay.compositionLayer.quad.size.width = overlay.widthMeters * xscale;
				overlay.compositionLayer.quad.size.height = overlay.widthMeters / aspect * yscale;
			}
			// The transform is in the current tracking space, which the app may have changed since it set the texture
			overlay.compositionLayer.base.space = xr_space_from_ref_space_type(GetUnsafeBaseSystem()->currentSpace);

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
	if (overlays.count(pchOverlayKey)) {
		OVL = overlays[pchOverlayKey];
		return VROverlayError_None;
	}

	// TODO is this the correct return value
	return VROverlayError_InvalidParameter;
}
EVROverlayError BaseOverlay::CreateOverlay(const char* pchOverlayKey, const char* pchOverlayName, VROverlayHandle_t* pOverlayHandle)
{
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
	data->compositionLayer.base.space = xr_space_from_ref_space_type(GetUnsafeBaseSystem()->currentSpace);

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
uint32_t BaseOverlay::GetOverlayKey(VROverlayHandle_t ulOverlayHandle, char* pchValue, uint32_t unBufferSize, EVROverlayError* pError)
{
	OverlayData* overlay = (OverlayData*)ulOverlayHandle;
	if (!overlays.count(overlay->key)) {
		if (pError)
			*pError = VROverlayError_InvalidHandle;
		if (unBufferSize != 0)
			pchValue = 0;
		return 0;
	}

	const char* key = overlay->key.c_str();
	strncpy_s(pchValue, unBufferSize, key, unBufferSize);

	if (strlen(key) >= unBufferSize && unBufferSize != 0) {
		pchValue[unBufferSize - 1] = 0;
	}

	if (pError)
		*pError = VROverlayError_None;

	// Is this supposed to include the NULL or not?
	// TODO test, this could cause some very nasty bugs
	return static_cast<uint32_t>(strlen(pchValue) + 1);
}
uint32_t BaseOverlay::GetOverlayName(VROverlayHandle_t ulOverlayHandle, VR_OUT_STRING() char* pchValue, uint32_t unBufferSize, EVROverlayError* pError)
{
	if (pError)
		*pError = VROverlayError_None;

	OverlayData* overlay = (OverlayData*)ulOverlayHandle;
	if (!overlays.count(overlay->key)) {
		if (pError)
			*pError = VROverlayError_InvalidHandle;
		if (unBufferSize != 0)
			pchValue[0] = 0;
		return 0;
	}

	const char* name = overlay->name.c_str();
	strncpy_s(pchValue, unBufferSize, name, unBufferSize);

	if (strlen(name) >= unBufferSize && unBufferSize != 0) {
		pchValue[unBufferSize - 1] = 0;
	}

	// Is this supposed to include the NULL or not?
	// TODO test, this could cause some very nasty bugs
	return static_cast<uint32_t>(strlen(pchValue) + 1);
}
EVROverlayError BaseOverlay::SetOverlayName(VROverlayHandle_t ulOverlayHandle, const char* pchName)
{
	USEH();

	overlay->name = pchName;

	return VROverlayError_None;
}
EVROverlayError BaseOverlay::GetOverlayImageData(VROverlayHandle_t ulOverlayHandle, void* pvBuffer, uint32_t unBufferSize, uint32_t* punWidth, uint32_t* punHeight)
{
	STUBBED();
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

	string msg = "Unknown overlay error code: " + to_string(error);
	OOVR_LOG(msg.c_str());

	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayRenderingPid(VROverlayHandle_t ulOverlayHandle, uint32_t unPID)
{
	STUBBED();
}
uint32_t BaseOverlay::GetOverlayRenderingPid(VROverlayHandle_t ulOverlayHandle)
{
	STUBBED();
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
		OOVR_ABORT("pfTexelAspect == nullptr");

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
	if (!xr_ext->CylinderKHR_Available()) {
		OOVR_LOGF("WARNING: Tried to set overlay curvature, but KHR_composition_layer_cylinder is not available.");
		return VROverlayError_None;
	}

	USEH();

	overlay->curvature = fCurvature;

	if (overlay->curvature > FLT_EPSILON) {
		overlay->compositionLayer.base.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
	} else {
		overlay->compositionLayer.base.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
	}

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
	if (pError)
		*pError = VROverlayError_None;

	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayRenderModel(VROverlayHandle_t ulOverlayHandle, const char* pchRenderModel, const HmdColor_t* pColor)
{
	STUBBED();
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
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayTransformTrackedDeviceComponent(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unDeviceIndex, const char* pchComponentName)
{
	STUBBED();
}
EVROverlayError BaseOverlay::GetOverlayTransformTrackedDeviceComponent(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t* punDeviceIndex, char* pchComponentName, uint32_t unComponentNameSize)
{
	STUBBED();
}
EVROverlayError BaseOverlay::GetOverlayTransformOverlayRelative(VROverlayHandle_t ulOverlayHandle, VROverlayHandle_t* ulOverlayHandleParent, HmdMatrix34_t* pmatParentOverlayToOverlayTransform)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayTransformOverlayRelative(VROverlayHandle_t ulOverlayHandle, VROverlayHandle_t ulOverlayHandleParent, const HmdMatrix34_t* pmatParentOverlayToOverlayTransform)
{
	// TODO
	return VROverlayError_None;
}
EVROverlayError BaseOverlay::SetOverlayTransformCursor(VROverlayHandle_t ulCursorOverlayHandle, const HmdVector2_t* pvHotspot)
{
	STUBBED();
}
EVROverlayError BaseOverlay::GetOverlayTransformCursor(VROverlayHandle_t ulOverlayHandle, HmdVector2_t* pvHotspot)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayTransformProjection(VROverlayHandle_t ulOverlayHandle,
    ETrackingUniverseOrigin eTrackingOrigin, const HmdMatrix34_t* pmatTrackingOriginToOverlayTransform,
    const OOVR_VROverlayProjection_t* pProjection, EVREye eEye)
{
	STUBBED();
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
	STUBBED();
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
	STUBBED();
}
bool BaseOverlay::IsHoverTargetOverlay(VROverlayHandle_t ulOverlayHandle)
{
	STUBBED();
}
VROverlayHandle_t BaseOverlay::GetGamepadFocusOverlay()
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetGamepadFocusOverlay(VROverlayHandle_t ulNewFocusOverlay)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayNeighbor(EOverlayDirection eDirection, VROverlayHandle_t ulFrom, VROverlayHandle_t ulTo)
{
	STUBBED();
}
EVROverlayError BaseOverlay::MoveGamepadFocusToNeighbor(EOverlayDirection eDirection, VROverlayHandle_t ulFrom)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayDualAnalogTransform(VROverlayHandle_t ulOverlay, EDualAnalogWhich eWhich, const HmdVector2_t& vCenter, float fRadius)
{
	STUBBED();
}
EVROverlayError BaseOverlay::GetOverlayDualAnalogTransform(VROverlayHandle_t ulOverlay, EDualAnalogWhich eWhich, HmdVector2_t* pvCenter, float* pfRadius)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayDualAnalogTransform(VROverlayHandle_t ulOverlay, EDualAnalogWhich eWhich, const HmdVector2_t* pvCenter, float fRadius)
{
	STUBBED();
}
EVROverlayError BaseOverlay::TriggerLaserMouseHapticVibration(VROverlayHandle_t ulOverlayHandle, float fDurationSeconds, float fFrequency, float fAmplitude)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayCursor(VROverlayHandle_t ulOverlayHandle, VROverlayHandle_t ulCursorHandle)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayCursorPositionOverride(VROverlayHandle_t ulOverlayHandle, const HmdVector2_t* pvCursor)
{
	STUBBED();
}
EVROverlayError BaseOverlay::ClearOverlayCursorPositionOverride(VROverlayHandle_t ulOverlayHandle)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayTexture(VROverlayHandle_t ulOverlayHandle, const Texture_t* pTexture)
{
	USEH();
	overlay->texture = *pTexture;

	BackendManager::Instance().OnOverlayTexture(&overlay->texture);

	if (!BackendManager::Instance().IsGraphicsConfigured())
		return VROverlayError_None;

	std::shared_ptr<Compositor> compositor = overlay->compositor.lock();
	if (!compositor) {
		compositor = GetUnsafeBaseCompositor()->CreateCompositorAPI(&overlay->texture);
		overlay->compositor = std::weak_ptr(compositor);
		auto* backend = (XrBackend*)BackendManager::Instance().GetBackendInstance();
		backend->RegisterOverlayCompositor(compositor);
	}

	compositor->LoadSubmitContext();
	//                                                             Same for quad & cylinder
	compositor->Invoke(&overlay->texture, &overlay->textureBounds, overlay->compositionLayer.quad.subImage);
	compositor->ResetSubmitContext();

	overlay->compositionLayer.base.space = xr_space_from_ref_space_type(GetUnsafeBaseSystem()->currentSpace);

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
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayFromFile(VROverlayHandle_t ulOverlayHandle, const char* pchFilePath)
{
	STUBBED();
}
EVROverlayError BaseOverlay::GetOverlayTexture(VROverlayHandle_t ulOverlayHandle, void** pNativeTextureHandle, void* pNativeTextureRef, uint32_t* pWidth, uint32_t* pHeight, uint32_t* pNativeFormat, ETextureType* pAPIType, EColorSpace* pColorSpace, VRTextureBounds_t* pTextureBounds)
{
	STUBBED();
}
EVROverlayError BaseOverlay::ReleaseNativeOverlayHandle(VROverlayHandle_t ulOverlayHandle, void* pNativeTextureHandle)
{
	STUBBED();
}
EVROverlayError BaseOverlay::GetOverlayTextureSize(VROverlayHandle_t ulOverlayHandle, uint32_t* pWidth, uint32_t* pHeight)
{
	STUBBED();
}
EVROverlayError BaseOverlay::CreateDashboardOverlay(const char* pchOverlayKey, const char* pchOverlayFriendlyName, VROverlayHandle_t* pMainHandle, VROverlayHandle_t* pThumbnailHandle)
{
	STUBBED();
}
bool BaseOverlay::IsDashboardVisible()
{
	// TODO should this be based of whether Dash is open?
	// Probably, but handling focus opens some other issues as it triggers under other conditions.
	return false;
}
bool BaseOverlay::IsActiveDashboardOverlay(VROverlayHandle_t ulOverlayHandle)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetDashboardOverlaySceneProcess(VROverlayHandle_t ulOverlayHandle, uint32_t unProcessId)
{
	STUBBED();
}
EVROverlayError BaseOverlay::GetDashboardOverlaySceneProcess(VROverlayHandle_t ulOverlayHandle, uint32_t* punProcessId)
{
	STUBBED();
}
void BaseOverlay::ShowDashboard(const char* pchOverlayToShow)
{
	STUBBED();
}
TrackedDeviceIndex_t BaseOverlay::GetPrimaryDashboardDevice()
{
	STUBBED();
}
EVROverlayError BaseOverlay::ShowKeyboardWithDispatch(EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode,
    const char* pchDescription, uint32_t unCharMax, const char* pchExistingText, bool bUseMinimalMode, uint64_t uUserValue,
    VRKeyboard::eventDispatch_t eventDispatch)
{
	// Submit a KeyboardDone event since the actual keyboard is not implemented yet.  This allows certain games to still proceed instead of crash.
	SubmitPlaceholderKeyboardEvent(VREvent_KeyboardDone, eventDispatch, uUserValue);

#ifndef OC_XR_PORT
	if (!BaseCompositor::dxcomp)
		OOVR_ABORT("Keyboard currently only available on DX11 and DX10 games");

	if (eLineInputMode != k_EGamepadTextInputLineModeSingleLine)
		OOVR_ABORTF("Only single-line keyboard entry mode is currently supported (as opposed to ID=%d)", eLineInputMode);

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
	STUBBED();
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
	STUBBED();
}
uint32_t BaseOverlay::GetKeyboardText(char* pchText, uint32_t cchText)
{
	string str = keyboard ? VRKeyboard::CHAR_CONV.to_bytes(keyboard->contents()) : keyboardCache;

	// Since keyboard is not functional yet, return this default text (Adventurer because this fix was made specifically for Skyrim VR)
	str = "Adventurer";

	// FFS, strncpy is secure.
	strncpy_s(pchText, cchText, str.c_str(), cchText);

	// Ensure the array always ends in a NULL
	pchText[cchText - 1] = 0;

	// TODO is this supposed to return the length of the string including or excluding the cchText limit?
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
	if (!keyboard)
		OOVR_ABORT("Cannot set keyboard position when the keyboard is closed!");

#ifdef OC_XR_PORT
	XR_STUBBED();
#else
	BaseCompositor* compositor = GetUnsafeBaseCompositor();
	if (compositor && eTrackingOrigin != compositor->GetTrackingSpace()) {
		OOVR_ABORTF("Origin mismatch - current %d, requested %d", compositor->GetTrackingSpace(), eTrackingOrigin);
	}

	keyboard->SetTransform(*pmatTrackingOriginToKeyboardTransform);
#endif
}
void BaseOverlay::SetKeyboardPositionForOverlay(VROverlayHandle_t ulOverlayHandle, HmdRect2_t avoidRect)
{
	STUBBED();
}
EVROverlayError BaseOverlay::SetOverlayIntersectionMask(VROverlayHandle_t ulOverlayHandle, OOVR_VROverlayIntersectionMaskPrimitive_t* pMaskPrimitives, uint32_t unNumMaskPrimitives, uint32_t unPrimitiveSize)
{
	STUBBED();
}
EVROverlayError BaseOverlay::GetOverlayFlags(VROverlayHandle_t ulOverlayHandle, uint32_t* pFlags)
{
	STUBBED();
}
BaseOverlay::VRMessageOverlayResponse BaseOverlay::ShowMessageOverlay(const char* pchText, const char* pchCaption, const char* pchButton0Text, const char* pchButton1Text, const char* pchButton2Text, const char* pchButton3Text)
{
	STUBBED();
}
void BaseOverlay::CloseMessageOverlay()
{
	STUBBED();
}

EVROverlayError BaseOverlay::SetOverlayPreCurvePitch(vr::VROverlayHandle_t ulOverlayHandle, float fRadians)
{
	STUBBED();
}

EVROverlayError BaseOverlay::GetOverlayPreCurvePitch(vr::VROverlayHandle_t ulOverlayHandle, float* pfRadians)
{
	STUBBED();
}

EVROverlayError BaseOverlay::WaitFrameSync(uint32_t nTimeoutMs)
{
	STUBBED();
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
