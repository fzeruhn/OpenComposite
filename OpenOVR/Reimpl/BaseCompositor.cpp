#include "stdafx.h"
#include <numbers>
#define BASE_IMPL

#include "Misc/Config.h"

#include "convert.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>

using glm::mat4;
using glm::quat;
using glm::vec3;
using glm::vec4;

#include "BaseCompositor.h"
#include "BaseOverlay.h"

// For the left and right hand constants - TODO move them to their own file
#include "BaseSystem.h"
#include "generated/static_bases.gen.h"

// FIXME find a nice way to clean this up
#ifdef SUPPORT_VK
#include "../../DrvOpenXR/pub/DrvOpenXR.h"
#endif

#include "BaseClientCore.h"
#include "Drivers/Backend.h"
#include "Misc/Process.h"
#include "Misc/ScopeGuard.h"

using namespace vr;
using namespace IVRCompositor_022;

typedef int ovr_enum_t;

#define SESS (*ovr::session)

BaseCompositor::BaseCompositor()
{
}

BaseCompositor::~BaseCompositor()
{
}

void BaseCompositor::SetTrackingSpace(ETrackingUniverseOrigin eOrigin)
{
	XrReferenceSpaceType origin = XR_REFERENCE_SPACE_TYPE_STAGE;
	if (eOrigin == TrackingUniverseSeated) {
		origin = XR_REFERENCE_SPACE_TYPE_LOCAL;
	}

	BaseSystem::currentSpace = origin;
}

ETrackingUniverseOrigin BaseCompositor::GetTrackingSpace()
{
	if (BaseSystem::currentSpace == XR_REFERENCE_SPACE_TYPE_LOCAL) {
		return TrackingUniverseSeated;
	} else {
		return TrackingUniverseStanding;
	}
}

ovr_enum_t BaseCompositor::WaitGetPoses(TrackedDevicePose_t* renderPoseArray, uint32_t renderPoseArrayCount,
    TrackedDevicePose_t* gamePoseArray, uint32_t gamePoseArrayCount)
{

	// Assume this method isn't being called between frames, b/c it really shouldn't be.
	leftEyeSubmitted = false;
	rightEyeSubmitted = false;

	BackendManager::Instance().WaitForTrackingData();

	return GetLastPoses(renderPoseArray, renderPoseArrayCount, gamePoseArray, gamePoseArrayCount);
}

void BaseCompositor::GetSinglePoseRendering(ETrackingUniverseOrigin origin, TrackedDeviceIndex_t unDeviceIndex, TrackedDevicePose_t* pOutputPose)
{
	BackendManager::Instance().GetSinglePose(origin, unDeviceIndex, pOutputPose, ETrackingStateType::TrackingStateType_Rendering);
}

mat4 BaseCompositor::GetHandTransform()
{
	float deg_to_rad = std::numbers::pi / 180;

	// The angle offset between the Touch and Vive controllers.
	// If this is incorrect, virtual hands will feel off.
	float controller_offset_angle = 39.5;

	// When testing to try and find the correct value above, uncomment
	//  this to lock the controller perfectly flat.
	// ovrPose.ThePose.Orientation = { 0,0,0,1 };

	vec3 rotateAxis = vec3(1, 0, 0);
	quat rotation = glm::rotate(controller_offset_angle * deg_to_rad, rotateAxis); // count++ * 0.01f);

	mat4 transform(rotation);

	// Controller offset
	// Note this is about right, found by playing around in Unity until everything
	//  roughly lines up. If you want to contribute better numbers, please go ahead!
	transform[3] = vec4(0.0f, 0.0353f, -0.0451f, 1.0f);

	return transform;
}

mat4 BaseCompositor::GetTrackerTransform()
{
	// The amount the tracker should be offset
	float tracker_offset = 0.025f;

	mat4 transform(1.0f);

	transform[3] = vec4(0.0f, 0.0f, tracker_offset, 1.0f);

	return transform;
}

ovr_enum_t BaseCompositor::GetLastPoses(TrackedDevicePose_t* renderPoseArray, uint32_t renderPoseArrayCount,
    TrackedDevicePose_t* gamePoseArray, uint32_t gamePoseArrayCount)
{

	ETrackingUniverseOrigin origin = GetTrackingSpace();

	for (uint32_t i = 0; i < std::max(gamePoseArrayCount, renderPoseArrayCount); i++) {
		TrackedDevicePose_t* renderPose = NULL;
		TrackedDevicePose_t* gamePose = NULL;

		if (renderPoseArray) {
			renderPose = i < renderPoseArrayCount ? renderPoseArray + i : NULL;
		}

		if (gamePoseArray) {
			gamePose = i < gamePoseArrayCount ? gamePoseArray + i : NULL;
		}

		if (renderPose) {
			GetSinglePoseRendering(origin, i, renderPose);
		}

		if (gamePose) {
			if (renderPose) {
				*gamePose = *renderPose;
			} else {
				GetSinglePoseRendering(origin, i, gamePose);
			}
		}
	}

	return VRCompositorError_None;
}

ovr_enum_t BaseCompositor::GetLastPoseForTrackedDeviceIndex(TrackedDeviceIndex_t unDeviceIndex, TrackedDevicePose_t* pOutputPose,
    TrackedDevicePose_t* pOutputGamePose)
{

	if (unDeviceIndex >= k_unMaxTrackedDeviceCount) {
		return VRCompositorError_IndexOutOfRange;
	}

	TrackedDevicePose_t pose{};
	GetSinglePoseRendering(GetTrackingSpace(), unDeviceIndex, &pose);

	if (pOutputPose) {
		*pOutputPose = pose;
	}

	if (pOutputGamePose) {
		*pOutputGamePose = pose;
	}

	return VRCompositorError_None;
}

#if !defined(OC_XR_PORT) && defined(SUPPORT_DX) && defined(SUPPORT_DX11)
DX11Compositor* BaseCompositor::dxcomp;
#endif

std::unique_ptr<Compositor> BaseCompositor::CreateCompositorAPI(const vr::Texture_t* texture)
{
	std::unique_ptr<Compositor> comp;

	switch (texture->eType) {
#if defined(SUPPORT_GL)
	case TextureType_OpenGL: {
		// Double-cast to avoid a CLion warning
		comp = std::make_unique<GLCompositor>((GLuint)(intptr_t)texture->handle);
		break;
	}
#elif defined(SUPPORT_GLES)
	case TextureType_OpenGL: {
		// Double-cast to avoid a CLion warning
		comp = std::make_unique<GLESCompositor>();
		break;
	}
#endif
#if defined(SUPPORT_DX) && defined(SUPPORT_DX11)
	case TextureType_DirectX: {
		if (!oovr_global_configuration.DX10Mode())
			comp = std::make_unique<DX11Compositor>((ID3D11Texture2D*)texture->handle);

#if defined(SUPPORT_DX10) && !defined(OC_XR_PORT)
		else
			comp = std::make_unique<DX10Compositor>((ID3D10Texture2D*)texture->handle);

		dxcomp = (DX11Compositor*)comp;
#else
		else
			OOVR_LOG_ONCE("D3D10 textures aren't supported in the OpenXR port yet: no compositor");
#endif

		break;
	}
#endif
#ifdef SUPPORT_VK
	case TextureType_Vulkan: {
		comp = std::make_unique<VkCompositor>(texture);
		break;
	}
#endif
#if defined(SUPPORT_DX) && defined(SUPPORT_DX12)
	case TextureType_DirectX12: {
		comp = std::make_unique<DX12Compositor>((D3D12TextureData_t*)texture->handle);
		break;
	}
#endif
	default:
		// No compositor: the eye is left out, and Submit answers an error
		OOVR_LOG_ONCEF("[BaseCompositor::Submit] Unsupported texture type: %d", (int)texture->eType);
		break;
	}

	return comp;
}

ovr_enum_t BaseCompositor::Submit(EVREye eye, const Texture_t* texture, const VRTextureBounds_t* bounds, EVRSubmitFlags submitFlags)
{
	if (BaseClientCore::appType == vr::VRApplication_Background) {
		// As SteamVR answers a Background app that submits
		OOVR_LOG_ONCE("A VRApplication_Background app is submitting frames: refused");
		return VRCompositorError_IsNotSceneApplication;
	}
	if (!texture || (eye != Eye_Left && eye != Eye_Right))
		return VRCompositorError_InvalidTexture;

	bool isFirstEye = !leftEyeSubmitted && !rightEyeSubmitted;
	ovr_enum_t result = VRCompositorError_None;

	bool eyeState = false;
	if (eye == Eye_Left)
		eyeState = leftEyeSubmitted;
	else
		eyeState = rightEyeSubmitted;

	if (eyeState) {
		// The first texture for this eye stays
		OOVR_LOG_ONCE("An eye was submitted twice in one frame: the second is refused");
		return VRCompositorError_AlreadySubmitted;
	}

	if (eye == Eye_Left)
		leftEyeSubmitted = true;
	else
		rightEyeSubmitted = true;

	// Handle null textures
	// Rather surprisingly, it's perfectly valid to pass null textures to SteamVR. So far, I've
	// only seen this used in Sparc (see #19). If a game tries to do this, ensure that either neither or
	// both eyes do so, and don't actually send a frame to LibOVR.
	bool textureNull = texture->handle == nullptr;
	if (isFirstEye) {
		isNullRender = textureNull;
	} else if (textureNull != isNullRender) {
		// One eye null and the other not: this frame isn't submitted, and the next starts afresh
		OOVR_LOG_ONCE("One eye's texture is null and the other's isn't: that frame is dropped");
		leftEyeSubmitted = false;
		rightEyeSubmitted = false;
		return VRCompositorError_InvalidTexture;
	}

	{
		// Turns out games (Beat Saber in particular) really do not like the session being restarted
		// while they're in the middle of submitting a frame.
		auto lock = xr_session.lock_shared();
		if (!textureNull)
			result = (ovr_enum_t)BackendManager::Instance().StoreEyeTexture(eye, texture, bounds, submitFlags, isFirstEye);

		if (leftEyeSubmitted && rightEyeSubmitted) {
			if (!isNullRender)
				BackendManager::Instance().SubmitFrames(isInSkybox, false);

			leftEyeSubmitted = false;
			rightEyeSubmitted = false;
		}
	}

	return result;
}

ovr_enum_t BaseCompositor::SubmitWithArrayIndex(vr::EVREye eye, const vr::Texture_t* texture, uint32_t idx, const vr::VRTextureBounds_t* bounds, vr::EVRSubmitFlags submitFlags)
{
	// The texture is one array texture, and idx its slice (not an index into an array of Texture_t); slices beyond
	// the first aren't supported yet, so the texture is submitted whole
	if (!texture)
		return VRCompositorError_InvalidTexture;
	if (idx != 0)
		OOVR_LOG_ONCE("SubmitWithArrayIndex: only slice 0 of array textures is used");
	return Submit(eye, texture, bounds, submitFlags);
}

void BaseCompositor::ClearLastSubmittedFrame()
{
	// At this point we should show the loading screen and show Guardian, and undo this when the
	// next frame comes along. TODO implement since it would improve loading screens, but it's certainly not critical
}

void BaseCompositor::PostPresentHandoff()
{
	// It appears (from the documentation) that SteamVR will, even after all frames are submitted, not begin
	//  compositing the submitted textures until WaitGetPoses is called. Thus is you want to do some rendering
	//  or game logic or whatever, it will delay the compositor. Calling this tells SteamVR that no further changes
	//  are to be made to the frame, and it can begin the compositor - in the aforementioned cases, this would be
	//  called directly after the last Submit call.
	//
	// Some apps provide a GUI through layers that are submitted after any eye textures are submitted. The app then
	// calls PostPresentHandOff to signal that all data is submitted and compositor can start work. Mimick this by
	// calling SubmitFrames with a flag to say it is called from this function.

	// As in Submit: the session isn't restarted while the frame is ended
	auto lock = xr_session.lock_shared();
	BackendManager::Instance().SubmitFrames(isInSkybox, true);
}

bool BaseCompositor::GetFrameTiming(OOVR_Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	// "Sets oldest timing info if nFramesAgo is larger than the stored history." So if our history
	// of timing records is only 1 we can just return that.

	return BackendManager::Instance().GetFrameTiming(pTiming, unFramesAgo);

	// TODO fill in the m_nNumVSyncsReadyForUse and uint32_t m_nNumVSyncsToFirstView fields, but only
	// when called from the correct version of the interface.
}

uint32_t BaseCompositor::GetFrameTimings(OOVR_Compositor_FrameTiming* pTiming, uint32_t nFrames)
{
	// This is a request to fill out an array of timing data. However only an arbitrary number
	// of records are available with number being filled returned. In the case of only one record
	// being available we can just send the most recent timing data and return 1.
	if (nFrames == 0)
		return 0;
	bool populated = BackendManager::Instance().GetFrameTiming(pTiming, 1);
	return populated ? 1 : 0;
}

bool BaseCompositor::GetFrameTiming(vr::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetFrameTiming((OOVR_Compositor_FrameTiming*)pTiming, unFramesAgo);
}

uint32_t BaseCompositor::GetFrameTimings(vr::Compositor_FrameTiming* pTiming, uint32_t nFrames)
{
	return GetFrameTimings((OOVR_Compositor_FrameTiming*)pTiming, nFrames);
}

float BaseCompositor::GetFrameTimeRemaining()
{
	// OpenXR doesn't say how long is left before the compositor needs the frame
	return 0.0f;
}

void BaseCompositor::GetCumulativeStats(OOVR_Compositor_CumulativeStats* pStats, uint32_t nStatsSizeInBytes)
{
	if (!pStats || nStatsSizeInBytes == 0)
		return;
	// Zeroed within the size the app gave (older SDKs have a shorter struct), with this process's id
	size_t size = std::min<size_t>(nStatsSizeInBytes, sizeof(OOVR_Compositor_CumulativeStats));
	memset(pStats, 0, size);
	if (size >= sizeof(uint32_t))
		pStats->m_nPid = oc_current_pid();
	OC_UNSUPPORTED_VOID("GetCumulativeStats: no frame statistics, all zero");
}

void BaseCompositor::FadeToColor(float fSeconds, float fRed, float fGreen, float fBlue, float fAlpha, bool bBackground)
{
	fadeTime = fSeconds;
	fadeColour.r = fRed;
	fadeColour.g = fGreen;
	fadeColour.b = fBlue;
	fadeColour.a = fAlpha;

	// TODO what does background do?
}

HmdColor_t BaseCompositor::GetCurrentFadeColor(bool bBackground)
{
	return fadeColour;
}

void BaseCompositor::FadeGrid(float fSeconds, bool bFadeIn)
{
	// This is the app telling SteamVR to fade from the rendered scene into the skybox, eg before the
	//  app loads a new level (this is how the default SteamVR Unity plugin works).
	//
	// Let's not bother implementing the fade (that would be a LOT of work), and just skip straight over.
	isInSkybox = bFadeIn;

	// TODO suppress input while in this mode
}

float BaseCompositor::GetCurrentGridAlpha()
{
	return isInSkybox ? 1.0f : 0.0f;
}

ovr_enum_t BaseCompositor::SetSkyboxOverride(const Texture_t* pTextures, uint32_t unTextureCount)
{
	return BackendManager::Instance().SetSkyboxOverride(pTextures, unTextureCount);
}

uint32_t BaseCompositor::GetLastError(char* pchBuffer, uint32_t unBufferSize)
{
	// Errors are answered by each call's result; there's no last error kept
	if (pchBuffer && unBufferSize)
		pchBuffer[0] = '\0';
	return 0;
}

void BaseCompositor::SetVSync(bool bVSync)
{
	oldVSync = bVSync;
}

bool BaseCompositor::GetVSync()
{
	return oldVSync;
}

void BaseCompositor::SetGamma(float fGamma)
{
	oldGamma = fGamma;
}

float BaseCompositor::GetGamma()
{
	return oldGamma;
}

void BaseCompositor::SetGraphicsDevice(OOVR_Compositor_DeviceType eType, void* pDevice)
{
	// The device itself comes with each texture (D3D11's GetDevice, OpenGL's current context), as for today's Submit
	oldDeviceType = eType;
}

bool BaseCompositor::OldDeviceTextureType(ETextureType* pType) const
{
	switch (oldDeviceType) {
	case OOVR_Compositor_DeviceType_D3D11:
		*pType = TextureType_DirectX;
		return true;
	case OOVR_Compositor_DeviceType_OpenGL:
		*pType = TextureType_OpenGL;
		return true;
	default:
		// None (SetGraphicsDevice wasn't called), D3D9, D3D9Ex or D3D10: nothing OpenComposite can show
		return false;
	}
}

BaseCompositor::ovr_enum_t BaseCompositor::SubmitForDevice(EVREye eEye, void* pTexture, const VRTextureBounds_t* pBounds)
{
	Texture_t texture = { pTexture, TextureType_DirectX, ColorSpace_Auto };
	if (!OldDeviceTextureType(&texture.eType))
		OC_UNSUPPORTED(VRCompositorError_InvalidTexture, "Submit (IVRCompositor_005/006): a texture of device type %d "
		                                                 "(only D3D11 and OpenGL can be shown)", (int)oldDeviceType);
	return Submit(eEye, &texture, pBounds, Submit_Default);
}

BaseCompositor::ovr_enum_t BaseCompositor::SubmitWithConvention(EVREye eEye, EGraphicsAPIConvention eTextureType, void* pTexture,
    const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags)
{
	// API_DirectX and API_OpenGL are TextureType_DirectX and TextureType_OpenGL
	Texture_t texture = { pTexture, (ETextureType)eTextureType, ColorSpace_Auto };
	return Submit(eEye, &texture, pBounds, nSubmitFlags);
}

BaseCompositor::ovr_enum_t BaseCompositor::SetSkyboxOverrideWithConvention(EGraphicsAPIConvention eTextureType, void* pFront,
    void* pBack, void* pLeft, void* pRight, void* pTop, void* pBottom)
{
	Texture_t textures[6];
	void* faces[6] = { pFront, pBack, pLeft, pRight, pTop, pBottom };
	for (int i = 0; i < 6; i++)
		textures[i] = { faces[i], (ETextureType)eTextureType, ColorSpace_Auto };
	return SetSkyboxOverride(textures, 6);
}

void BaseCompositor::GetOverlayDefaults(Compositor_OverlaySettings* pSettings)
{
	if (!pSettings)
		return;
	// The settings a scene overlay would have, though none is drawn: a flat, opaque quad a metre away, its whole texture
	*pSettings = {};
	pSettings->size = sizeof(Compositor_OverlaySettings);
	pSettings->antialias = true;
	pSettings->scale = 1.0f;
	pSettings->distance = 1.0f;
	pSettings->alpha = 1.0f;
	pSettings->uScale = 1.0f;
	pSettings->vScale = 1.0f;
	for (int i = 0; i < 4; i++)
		pSettings->transform.m[i][i] = 1.0f;
}

void BaseCompositor::SetOverlay(void* pTexture, Compositor_OverlaySettings* pSettings)
{
	OC_UNSUPPORTED_VOID("SetOverlay (IVRCompositor_005): no scene overlay is drawn");
}

void BaseCompositor::SetOverlayRaw(void* buffer, uint32_t width, uint32_t height, uint32_t depth, Compositor_OverlaySettings* pSettings)
{
	OC_UNSUPPORTED_VOID("SetOverlayRaw (IVRCompositor_005): no scene overlay is drawn");
}

void BaseCompositor::SetOverlayFromFile(const char* pchFilePath, Compositor_OverlaySettings* pSettings)
{
	OC_UNSUPPORTED_VOID("SetOverlayFromFile (IVRCompositor_005): no scene overlay is drawn");
}

void BaseCompositor::ClearOverlay()
{
	// No overlay is drawn, so none is left to clear
}

bool BaseCompositor::ComputeOverlayIntersection(const Compositor_OverlaySettings* pSettings, float fAspectRatio,
    ETrackingUniverseOrigin eOrigin, HmdVector3_t vSource, HmdVector3_t vDirection,
    HmdVector2_t* pvecIntersectionUV, HmdVector3_t* pvecIntersectionTrackingSpace)
{
	// No overlay is drawn: no intersection
	return false;
}

void BaseCompositor::ClearSkyboxOverride()
{
	BackendManager::Instance().ClearSkyboxOverride();
}

void BaseCompositor::CompositorBringToFront()
{
	// No actions required, Oculus runs via direct mode
}

void BaseCompositor::CompositorGoToBack()
{
	// The OpenXR runtime's compositor owns the headset; there's nothing to send back
}

void BaseCompositor::CompositorQuit()
{
	OC_UNSUPPORTED_VOID("CompositorQuit: the OpenXR runtime's compositor isn't ours to quit");
}

bool BaseCompositor::IsFullscreen()
{
	// Direct mode, as SteamVR's compositor
	return true;
}

uint32_t BaseCompositor::GetCurrentSceneFocusProcess()
{
	return oc_current_pid();
}

uint32_t BaseCompositor::GetLastFrameRenderer()
{
	return oc_current_pid();
}

bool BaseCompositor::CanRenderScene()
{
	return true; // TODO implement
}

void BaseCompositor::ShowMirrorWindow()
{
	OC_UNSUPPORTED_VOID("ShowMirrorWindow: no mirror window (the OpenXR runtime may have its own)");
}

void BaseCompositor::HideMirrorWindow()
{
	// There's no mirror window to hide
}

bool BaseCompositor::IsMirrorWindowVisible()
{
	return false;
}

void BaseCompositor::CompositorDumpImages()
{
	OC_UNSUPPORTED_VOID("CompositorDumpImages: not supported");
}

bool BaseCompositor::ShouldAppRenderWithLowResources()
{
	// TODO put in config file
	return false;
}

void BaseCompositor::ForceInterleavedReprojectionOn(bool bOverride)
{
	// Force timewarp on? Yeah right.
}

void BaseCompositor::ForceReconnectProcess()
{
	// We should always be connected
}

void BaseCompositor::SuspendRendering(bool bSuspend)
{
	// TODO
	// I'm not sure what the purpose of this function is. If you know, please tell me.
	// - ZNix
	// STUBBED();
}

ovr_enum_t BaseCompositor::GetMirrorTextureD3D11(EVREye eEye, void* pD3D11DeviceOrResource, void** ppD3D11ShaderResourceView)
{
#if defined(SUPPORT_DX) && defined(SUPPORT_DX11)
	return BackendManager::Instance().GetMirrorTextureD3D11(eEye, pD3D11DeviceOrResource, ppD3D11ShaderResourceView);
#else
	if (ppD3D11ShaderResourceView)
		*ppD3D11ShaderResourceView = nullptr;
	OC_UNSUPPORTED(VRCompositorError_RequestFailed, "GetMirrorTextureD3D11: this build has no D3D11 support");
#endif
}

void BaseCompositor::ReleaseMirrorTextureD3D11(void* pD3D11ShaderResourceView)
{
#if defined(SUPPORT_DX) && defined(SUPPORT_DX11)
	return BackendManager::Instance().ReleaseMirrorTextureD3D11(pD3D11ShaderResourceView);
#else
	// GetMirrorTextureD3D11 handed nothing out in this build
#endif
}

ovr_enum_t BaseCompositor::GetMirrorTextureGL(EVREye eEye, glUInt_t* pglTextureId, glSharedTextureHandle_t* pglSharedTextureHandle)
{
	if (pglTextureId)
		*pglTextureId = 0;
	if (pglSharedTextureHandle)
		*pglSharedTextureHandle = nullptr;
	OC_UNSUPPORTED(VRCompositorError_RequestFailed, "GetMirrorTextureGL: no OpenGL mirror texture");
}

bool BaseCompositor::ReleaseSharedGLTexture(glUInt_t glTextureId, glSharedTextureHandle_t glSharedTextureHandle)
{
	// GetMirrorTextureGL hands nothing out
	return false;
}

void BaseCompositor::LockGLSharedTextureForAccess(glSharedTextureHandle_t glSharedTextureHandle)
{
	// GetMirrorTextureGL hands nothing out, so there's nothing to lock
}

void BaseCompositor::UnlockGLSharedTextureForAccess(glSharedTextureHandle_t glSharedTextureHandle)
{
	// GetMirrorTextureGL hands nothing out, so there's nothing to unlock
}

uint32_t BaseCompositor::GetVulkanInstanceExtensionsRequired(char* pchValue, uint32_t unBufferSize)
{
	if (pchValue && unBufferSize)
		pchValue[0] = 0;
#if defined(SUPPORT_VK)
	// Whaddya know, the OpenXR, Oculus and Valve methods work almost identically...
	uint32_t size = 0;
	XrResult res = xr_ext->xrGetVulkanInstanceExtensionsKHR(xr_instance, xr_system, unBufferSize, &size, pchValue);
	if (res != XR_ERROR_SIZE_INSUFFICIENT)
		OC_XR_SOFT(res, 0);
	return VulkanExtensionsSize(res, size);
#else
	OC_UNSUPPORTED(0, "GetVulkanInstanceExtensionsRequired: this build has no Vulkan support");
#endif
}

uint32_t BaseCompositor::GetVulkanDeviceExtensionsRequired(VkPhysicalDevice_T* pPhysicalDevice, char* pchValue, uint32_t unBufferSize)
{
	if (pchValue && unBufferSize)
		pchValue[0] = 0;
#if defined(SUPPORT_VK)
	uint32_t size = 0;
	XrResult res = xr_ext->xrGetVulkanDeviceExtensionsKHR(xr_instance, xr_system, unBufferSize, &size, pchValue);
	if (res != XR_ERROR_SIZE_INSUFFICIENT)
		OC_XR_SOFT(res, 0);
	return VulkanExtensionsSize(res, size);
#else
	OC_UNSUPPORTED(0, "GetVulkanDeviceExtensionsRequired: this build has no Vulkan support");
#endif
}

void BaseCompositor::SetExplicitTimingMode(ovr_enum_t eTimingMode)
{
	// Explicit timing means the application calls SubmitExplicitTimingData each
	// frame, and in return we're not allowed to use their Vulkan queue
	// during WaitGetPoses. We don't do any of that anyway, so nothing needs to
	// be done here.
}

ovr_enum_t BaseCompositor::SubmitExplicitTimingData()
{
	// In SteamVR this records a more accurate timestamp for tracking via the GPU's
	// clock, Oculus doesn't support that so noop here is fine.
	return VRCompositorError_None;
}

bool BaseCompositor::IsMotionSmoothingSupported()
{
	return false;
}

bool BaseCompositor::IsMotionSmoothingEnabled()
{
	return false;
}

bool BaseCompositor::IsCurrentSceneFocusAppLoading()
{
	return isInSkybox;
}

ovr_enum_t BaseCompositor::SetStageOverride_Async(const char* pchRenderModelPath, const HmdMatrix34_t* pTransform,
    const OOVR_Compositor_StageRenderSettings* pRenderSettings, uint32_t nSizeOfRenderSettings)
{
	OOVR_SOFT_ABORT("Stage override not implemented");
	return VRCompositorError_None;
}

void BaseCompositor::ClearStageOverride()
{
	OOVR_SOFT_ABORT("Stage override not implemented");
}

bool BaseCompositor::GetCompositorBenchmarkResults(Compositor_BenchmarkResults* pBenchmarkResults, uint32_t nSizeOfBenchmarkResults)
{
	OOVR_SOFT_ABORT("Compositor benchmarking not implemented");
	return false;
}

ovr_enum_t BaseCompositor::GetLastPosePredictionIDs(uint32_t* pRenderPosePredictionID, uint32_t* pGamePosePredictionID)
{
	OOVR_SOFT_ABORT("Pose prediction IDs hardcoded at 0");
	if (pRenderPosePredictionID)
		*pRenderPosePredictionID = 0;
	if (pGamePosePredictionID)
		*pGamePosePredictionID = 0;
	return VRCompositorError_None;
}

ovr_enum_t BaseCompositor::GetPosesForFrame(uint32_t unPosePredictionID, TrackedDevicePose_t* pPoseArray, uint32_t unPoseArrayCount)
{
	// Pose prediction ids are always 0 (GetLastPosePredictionIDs), so there's no earlier frame's poses to give
	if (pPoseArray)
		memset(pPoseArray, 0, sizeof(TrackedDevicePose_t) * unPoseArrayCount);
	OC_UNSUPPORTED(VRCompositorError_RequestFailed, "GetPosesForFrame: no pose prediction ids");
}

ovr_enum_t BaseCompositor::GetSubmitTexture(Texture_t* pOutTexture, bool* pNeedsFlush, OOVR_EVRCompositorTextureUsage eUsage,
    const Texture_t* pTexture, const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags)
{
	// There's no compositor-owned shared texture to give out.
	return VRCompositorError_RequestFailed;
}
