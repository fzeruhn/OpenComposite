#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("Overlay", "001")
GEN_INTERFACE("Overlay", "002")
GEN_INTERFACE("Overlay", "003")
GEN_INTERFACE("Overlay", "004")
GEN_INTERFACE("Overlay", "005")
GEN_INTERFACE("Overlay", "006")
GEN_INTERFACE("Overlay", "007")
GEN_INTERFACE("Overlay", "008")
GEN_INTERFACE("Overlay", "010")
GEN_INTERFACE("Overlay", "011")
// TODO 012
GEN_INTERFACE("Overlay", "012")
GEN_INTERFACE("Overlay", "013")
GEN_INTERFACE("Overlay", "014")
// TODO 015
GEN_INTERFACE("Overlay", "016")
GEN_INTERFACE("Overlay", "017")
GEN_INTERFACE("Overlay", "018")
GEN_INTERFACE("Overlay", "019")
GEN_INTERFACE("Overlay", "020")
GEN_INTERFACE("Overlay", "021")
GEN_INTERFACE("Overlay", "022")
// Version 023 never appears in a public release
GEN_INTERFACE("Overlay", "024")
GEN_INTERFACE("Overlay", "025")
GEN_INTERFACE("Overlay", "026")
GEN_INTERFACE("Overlay", "027")
GEN_INTERFACE("Overlay", "028")

#include "generated/GVROverlay.gen.h"

// Up to IVROverlay_008 (SDK 0.9.14) the event is the old 32-byte one
bool CVROverlay_001::PollNextOverlayEvent(vr::IVROverlay_001::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t* pEvent)
{
	return base->PollNextOverlayEventOld(ulOverlayHandle, pEvent);
}

bool CVROverlay_002::PollNextOverlayEvent(vr::IVROverlay_002::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t* pEvent)
{
	return base->PollNextOverlayEventOld(ulOverlayHandle, pEvent);
}

bool CVROverlay_003::PollNextOverlayEvent(vr::IVROverlay_003::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t* pEvent)
{
	return base->PollNextOverlayEventOld(ulOverlayHandle, pEvent);
}

bool CVROverlay_004::PollNextOverlayEvent(vr::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t* pEvent)
{
	return base->PollNextOverlayEventOld(ulOverlayHandle, pEvent);
}

bool CVROverlay_005::PollNextOverlayEvent(vr::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t* pEvent)
{
	return base->PollNextOverlayEventOld(ulOverlayHandle, pEvent);
}

bool CVROverlay_006::PollNextOverlayEvent(vr::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t* pEvent)
{
	return base->PollNextOverlayEventOld(ulOverlayHandle, pEvent);
}

bool CVROverlay_007::PollNextOverlayEvent(vr::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t* pEvent)
{
	return base->PollNextOverlayEventOld(ulOverlayHandle, pEvent);
}

bool CVROverlay_008::PollNextOverlayEvent(vr::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t* pEvent)
{
	return base->PollNextOverlayEventOld(ulOverlayHandle, pEvent);
}

// IVROverlay_001 takes a bare texture of the device IVRCompositor::SetGraphicsDevice named; 002-006 one of the API given
vr::IVROverlay_001::VROverlayError CVROverlay_001::SetOverlayTexture(vr::IVROverlay_001::VROverlayHandle_t ulOverlayHandle, void* pTexture)
{
	return (vr::IVROverlay_001::VROverlayError)base->SetOverlayTextureForDevice(ulOverlayHandle, pTexture);
}

vr::IVROverlay_002::VROverlayError CVROverlay_002::SetOverlayTexture(vr::IVROverlay_002::VROverlayHandle_t ulOverlayHandle,
    vr::GraphicsAPIConvention eTextureType, void* pTexture)
{
	return (vr::IVROverlay_002::VROverlayError)base->SetOverlayTextureWithConvention(ulOverlayHandle, eTextureType, pTexture);
}

vr::IVROverlay_003::VROverlayError CVROverlay_003::SetOverlayTexture(vr::IVROverlay_003::VROverlayHandle_t ulOverlayHandle,
    vr::GraphicsAPIConvention eTextureType, void* pTexture)
{
	return (vr::IVROverlay_003::VROverlayError)base->SetOverlayTextureWithConvention(ulOverlayHandle, eTextureType, pTexture);
}

vr::VROverlayError CVROverlay_004::SetOverlayTexture(vr::VROverlayHandle_t ulOverlayHandle, vr::GraphicsAPIConvention eTextureType, void* pTexture)
{
	return base->SetOverlayTextureWithConvention(ulOverlayHandle, eTextureType, pTexture);
}

vr::VROverlayError CVROverlay_005::SetOverlayTexture(vr::VROverlayHandle_t ulOverlayHandle, vr::GraphicsAPIConvention eTextureType, void* pTexture)
{
	return base->SetOverlayTextureWithConvention(ulOverlayHandle, eTextureType, pTexture);
}

vr::VROverlayError CVROverlay_006::SetOverlayTexture(vr::VROverlayHandle_t ulOverlayHandle, vr::GraphicsAPIConvention eTextureType, void* pTexture)
{
	return base->SetOverlayTextureWithConvention(ulOverlayHandle, eTextureType, pTexture);
}

vr::EVROverlayError CVROverlay_012::GetOverlayTexture(vr::VROverlayHandle_t ulOverlayHandle, void** pNativeTextureHandle,
    void* pNativeTextureRef, uint32_t* pWidth, uint32_t* pHeight, uint32_t* pNativeFormat, EGraphicsAPIConvention* pAPI,
    vr::EColorSpace* pColorSpace)
{
	// The old versions name the graphics API with EGraphicsAPIConvention (DirectX 0, OpenGL 1), as ETextureType does
	vr::ETextureType type = vr::TextureType_DirectX;
	vr::EVROverlayError err = base->GetOverlayTexture(ulOverlayHandle, pNativeTextureHandle, pNativeTextureRef, pWidth, pHeight,
	    pNativeFormat, &type, pColorSpace, nullptr);
	if (pAPI)
		*pAPI = (EGraphicsAPIConvention)type;
	return err;
}

vr::EVROverlayError CVROverlay_011::GetOverlayTexture(vr::VROverlayHandle_t ulOverlayHandle, void** pNativeTextureHandle,
    void* pNativeTextureRef, uint32_t* pWidth, uint32_t* pHeight, uint32_t* pNativeFormat, EGraphicsAPIConvention* pAPI,
    vr::EColorSpace* pColorSpace)
{
	// The old versions name the graphics API with EGraphicsAPIConvention (DirectX 0, OpenGL 1), as ETextureType does
	vr::ETextureType type = vr::TextureType_DirectX;
	vr::EVROverlayError err = base->GetOverlayTexture(ulOverlayHandle, pNativeTextureHandle, pNativeTextureRef, pWidth, pHeight,
	    pNativeFormat, &type, pColorSpace, nullptr);
	if (pAPI)
		*pAPI = (EGraphicsAPIConvention)type;
	return err;
}

vr::EVROverlayError CVROverlay_013::GetOverlayTexture(vr::VROverlayHandle_t ulOverlayHandle, void** pNativeTextureHandle,
    void* pNativeTextureRef, uint32_t* pWidth, uint32_t* pHeight, uint32_t* pNativeFormat, EGraphicsAPIConvention* pAPI,
    vr::EColorSpace* pColorSpace)
{
	// The old versions name the graphics API with EGraphicsAPIConvention (DirectX 0, OpenGL 1), as ETextureType does
	vr::ETextureType type = vr::TextureType_DirectX;
	vr::EVROverlayError err = base->GetOverlayTexture(ulOverlayHandle, pNativeTextureHandle, pNativeTextureRef, pWidth, pHeight,
	    pNativeFormat, &type, pColorSpace, nullptr);
	if (pAPI)
		*pAPI = (EGraphicsAPIConvention)type;
	return err;
}

// From IVROverlay_021 (SDK 1.8.19) a flag is passed as its mask (VROverlayFlags_NoDashboardTab = 1 << 3); before, as
// its bit number (3), which BaseOverlay::SetOverlayFlag takes

vr::EVROverlayError CVROverlay_021::SetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_021::VROverlayFlags eOverlayFlag, bool bEnabled)
{
	return base->SetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, bEnabled);
}

vr::EVROverlayError CVROverlay_021::GetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_021::VROverlayFlags eOverlayFlag, bool* pbEnabled)
{
	return base->GetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, pbEnabled);
}

vr::EVROverlayError CVROverlay_022::SetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_022::VROverlayFlags eOverlayFlag, bool bEnabled)
{
	return base->SetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, bEnabled);
}

vr::EVROverlayError CVROverlay_022::GetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_022::VROverlayFlags eOverlayFlag, bool* pbEnabled)
{
	return base->GetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, pbEnabled);
}

vr::EVROverlayError CVROverlay_024::SetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_024::VROverlayFlags eOverlayFlag, bool bEnabled)
{
	return base->SetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, bEnabled);
}

vr::EVROverlayError CVROverlay_024::GetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_024::VROverlayFlags eOverlayFlag, bool* pbEnabled)
{
	return base->GetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, pbEnabled);
}

vr::EVROverlayError CVROverlay_025::SetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_025::VROverlayFlags eOverlayFlag, bool bEnabled)
{
	return base->SetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, bEnabled);
}

vr::EVROverlayError CVROverlay_025::GetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_025::VROverlayFlags eOverlayFlag, bool* pbEnabled)
{
	return base->GetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, pbEnabled);
}

vr::EVROverlayError CVROverlay_026::SetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_026::VROverlayFlags eOverlayFlag, bool bEnabled)
{
	return base->SetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, bEnabled);
}

vr::EVROverlayError CVROverlay_026::GetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_026::VROverlayFlags eOverlayFlag, bool* pbEnabled)
{
	return base->GetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, pbEnabled);
}

vr::EVROverlayError CVROverlay_027::SetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_027::VROverlayFlags eOverlayFlag, bool bEnabled)
{
	return base->SetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, bEnabled);
}

vr::EVROverlayError CVROverlay_027::GetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_027::VROverlayFlags eOverlayFlag, bool* pbEnabled)
{
	return base->GetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, pbEnabled);
}

vr::EVROverlayError CVROverlay_028::SetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_028::VROverlayFlags eOverlayFlag, bool bEnabled)
{
	return base->SetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, bEnabled);
}

vr::EVROverlayError CVROverlay_028::GetOverlayFlag(vr::VROverlayHandle_t ulOverlayHandle, vr::IVROverlay_028::VROverlayFlags eOverlayFlag, bool* pbEnabled)
{
	return base->GetOverlayFlagMask(ulOverlayHandle, (uint32_t)eOverlayFlag, pbEnabled);
}
