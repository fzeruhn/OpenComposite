#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("Compositor", "005")
GEN_INTERFACE("Compositor", "006")
GEN_INTERFACE("Compositor", "007")
GEN_INTERFACE("Compositor", "008")
GEN_INTERFACE("Compositor", "009")
GEN_INTERFACE("Compositor", "010")
GEN_INTERFACE("Compositor", "011")
GEN_INTERFACE("Compositor", "012")
GEN_INTERFACE("Compositor", "013")
GEN_INTERFACE("Compositor", "014")
GEN_INTERFACE("Compositor", "015")
GEN_INTERFACE("Compositor", "016")
GEN_INTERFACE("Compositor", "017", CUSTOM)
GEN_INTERFACE("Compositor", "018")
GEN_INTERFACE("Compositor", "019")
GEN_INTERFACE("Compositor", "020")
GEN_INTERFACE("Compositor", "021")
GEN_INTERFACE("Compositor", "022")
// 023 is missing, but never appeared in a public header - it was bumped from 022 to 024 in OpenVR 1.9.16
GEN_INTERFACE("Compositor", "024")
// 025 is skipped, as per above
GEN_INTERFACE("Compositor", "026")
GEN_INTERFACE("Compositor", "027")
GEN_INTERFACE("Compositor", "028")
GEN_INTERFACE("Compositor", "029")

#include "generated/GVRCompositor.gen.h"

#include "Misc/Config.h"

using namespace vr;

// Screenshot functions, only in version 015, and marked as deprecated at that. It's highly
//  unlikely these will be used by any publicly-available application.
// Probably added by Valve for internal usage, and deprecated before it was released.
IVRCompositor_015::EVRCompositorError CVRCompositor_015::RequestScreenshot(EVRScreenshotType type,
    const char* pchDestinationFileName, const char* pchVRDestinationFileName)
{
	// Screenshots go through IVRScreenshots in later SDKs; this old route takes none
	OC_UNSUPPORTED(IVRCompositor_015::VRCompositorError_RequestFailed, "IVRCompositor_015::RequestScreenshot: no screenshots");
}

EVRScreenshotType CVRCompositor_015::GetCurrentScreenshotType()
{
	// RequestScreenshot takes none, so none is in progress
	return VRScreenshotType_None;
}

// Before IVRCompositor_017, Compositor_FrameTiming had other layouts than the one BaseCompositor fills, and a Linux
// build's can be smaller than the Windows one whose m_nSize Proton passes on. So fill ours, copy the fields the old
// struct shares, and write no more than the old struct holds. minSize: the smallest the version's struct ever was.
template <typename T>
static bool GetOldFrameTiming(BaseCompositor* base, T* pTiming, uint32_t unFramesAgo, const char* iface, size_t minSize = sizeof(T))
{
	if (oovr_global_configuration.LogAllOpenVRCalls())
		oovr_log_raw_format(__FILE__, __LINE__, "GetFrameTiming", "Entered function (from interface %s)", iface);
	if (!pTiming)
		return false;

	OOVR_Compositor_FrameTiming ours = {};
	ours.m_nSize = sizeof(ours);
	const bool filled = base->GetFrameTiming(&ours, unFramesAgo);

	T theirs = {};
	uint32_t appSize;
	if constexpr (requires(T t) { t.m_nSize; })
		appSize = pTiming->m_nSize;
	else
		appSize = pTiming->size; // IVRCompositor_009

#define COPY(theirField, ourField)                     \
	if constexpr (requires(T t) { t.theirField; }) \
		theirs.theirField = ours.ourField;
	COPY(m_nFrameIndex, m_nFrameIndex)
	COPY(m_nNumFramePresents, m_nNumFramePresents)
	COPY(m_nNumMisPresented, m_nNumMisPresented)
	COPY(m_nNumDroppedFrames, m_nNumDroppedFrames)
	COPY(m_nReprojectionFlags, m_nReprojectionFlags)
	COPY(m_flSystemTimeInSeconds, m_flSystemTimeInSeconds)
	COPY(m_flPreSubmitGpuMs, m_flPreSubmitGpuMs)
	COPY(m_flPostSubmitGpuMs, m_flPostSubmitGpuMs)
	COPY(m_flSceneRenderGpuMs, m_flPreSubmitGpuMs)
	COPY(m_flTotalRenderGpuMs, m_flTotalRenderGpuMs)
	COPY(m_flCompositorRenderGpuMs, m_flCompositorRenderGpuMs)
	COPY(m_flCompositorRenderCpuMs, m_flCompositorRenderCpuMs)
	COPY(m_flCompositorIdleCpuMs, m_flCompositorIdleCpuMs)
	COPY(m_flClientFrameIntervalMs, m_flClientFrameIntervalMs)
	COPY(m_flPresentCallCpuMs, m_flPresentCallCpuMs)
	COPY(m_flWaitForPresentCpuMs, m_flWaitForPresentCpuMs)
	COPY(m_flSubmitFrameMs, m_flSubmitFrameMs)
	COPY(m_flWaitGetPosesCalledMs, m_flWaitGetPosesCalledMs)
	COPY(m_flNewPosesReadyMs, m_flNewPosesReadyMs)
	COPY(m_flNewFrameReadyMs, m_flNewFrameReadyMs)
	COPY(m_flCompositorUpdateStartMs, m_flCompositorUpdateStartMs)
	COPY(m_flCompositorUpdateEndMs, m_flCompositorUpdateEndMs)
	COPY(m_flCompositorRenderStartMs, m_flCompositorRenderStartMs)
	COPY(m_HmdPose, m_HmdPose)
	// IVRCompositor_009's names
	COPY(frameStart, m_flSystemTimeInSeconds)
	COPY(frameIndex, m_nFrameIndex)
	COPY(droppedFrames, m_nNumDroppedFrames)
	COPY(pose, m_HmdPose)
	COPY(m_flFrameIntervalMs, m_flClientFrameIntervalMs)
#undef COPY

	const size_t sizeField = sizeof(uint32_t);
	const size_t size = std::min<size_t>(appSize, sizeof(T));
	if (size > sizeField)
		memcpy(reinterpret_cast<char*>(pTiming) + sizeField, reinterpret_cast<const char*>(&theirs) + sizeField, size - sizeField);
	return filled && appSize >= minSize;
}

// IVRCompositor_005-008 (SDK 0.9.0 to 0.9.11) take bare texture pointers: 005 and 006 of the API SetGraphicsDevice
// named, 007 and 008 of the API passed with each

void CVRCompositor_005::SetGraphicsDevice(IVRCompositor_005::Compositor_DeviceType eType, void* pDevice)
{
	base->SetGraphicsDevice((OOVR_Compositor_DeviceType)eType, pDevice);
}

void CVRCompositor_006::SetGraphicsDevice(IVRCompositor_006::Compositor_DeviceType eType, void* pDevice)
{
	base->SetGraphicsDevice((OOVR_Compositor_DeviceType)eType, pDevice);
}

void CVRCompositor_005::WaitGetPoses(TrackedDevicePose_t* pPoseArray, uint32_t unPoseArrayCount)
{
	base->WaitGetPoses(pPoseArray, unPoseArrayCount, nullptr, 0);
}

void CVRCompositor_005::Submit(Hmd_Eye eEye, void* pTexture, IVRCompositor_005::Compositor_TextureBounds* pBounds)
{
	// Compositor_TextureBounds is VRTextureBounds_t (uMin, vMin, uMax, vMax)
	base->SubmitForDevice(eEye, pTexture, (const VRTextureBounds_t*)pBounds);
}

IVRCompositor_006::VRCompositorError CVRCompositor_006::Submit(Hmd_Eye eEye, void* pTexture, VRTextureBounds_t* pBounds)
{
	return (IVRCompositor_006::VRCompositorError)base->SubmitForDevice(eEye, pTexture, pBounds);
}

IVRCompositor_007::VRCompositorError CVRCompositor_007::Submit(Hmd_Eye eEye, GraphicsAPIConvention eTextureType, void* pTexture,
    const VRTextureBounds_t* pBounds)
{
	return (IVRCompositor_007::VRCompositorError)base->SubmitWithConvention(eEye, eTextureType, pTexture, pBounds, Submit_Default);
}

IVRCompositor_008::VRCompositorError CVRCompositor_008::Submit(Hmd_Eye eEye, GraphicsAPIConvention eTextureType, void* pTexture,
    const VRTextureBounds_t* pBounds, VRSubmitFlags_t nSubmitFlags)
{
	return (IVRCompositor_008::VRCompositorError)base->SubmitWithConvention(eEye, eTextureType, pTexture, pBounds, nSubmitFlags);
}

void CVRCompositor_008::SetSkyboxOverride(GraphicsAPIConvention eTextureType, void* pFront, void* pBack, void* pLeft,
    void* pRight, void* pTop, void* pBottom)
{
	base->SetSkyboxOverrideWithConvention(eTextureType, pFront, pBack, pLeft, pRight, pTop, pBottom);
}

bool CVRCompositor_005::GetFrameTiming(IVRCompositor_005::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "IVRCompositor_005");
}
bool CVRCompositor_006::GetFrameTiming(IVRCompositor_006::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "IVRCompositor_006");
}
bool CVRCompositor_007::GetFrameTiming(IVRCompositor_007::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "IVRCompositor_007");
}
bool CVRCompositor_008::GetFrameTiming(IVRCompositor_008::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	// SDK 0.9.9's struct for 008 ends before the handoff times 0.9.11 added
	using T = IVRCompositor_008::Compositor_FrameTiming;
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "IVRCompositor_008", offsetof(T, m_flHandoffStartMs));
}
bool CVRCompositor_010::GetFrameTiming(IVRCompositor_010::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "IVRCompositor_010");
}
bool CVRCompositor_011::GetFrameTiming(IVRCompositor_011::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "IVRCompositor_011");
}
bool CVRCompositor_009::GetFrameTiming(IVRCompositor_009::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "vr::IVRCompositor_009");
}
bool CVRCompositor_012::GetFrameTiming(IVRCompositor_012::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "vr::IVRCompositor_012");
}
bool CVRCompositor_013::GetFrameTiming(IVRCompositor_013::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "vr::IVRCompositor_013");
}
bool CVRCompositor_014::GetFrameTiming(IVRCompositor_014::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "vr::IVRCompositor_014");
}
bool CVRCompositor_015::GetFrameTiming(IVRCompositor_015::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "vr::IVRCompositor_015");
}
bool CVRCompositor_016::GetFrameTiming(IVRCompositor_016::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo)
{
	return GetOldFrameTiming(base.get(), pTiming, unFramesAgo, "vr::IVRCompositor_016");
}
