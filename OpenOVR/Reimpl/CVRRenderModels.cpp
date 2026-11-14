#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("RenderModels", "001")
GEN_INTERFACE("RenderModels", "002")
GEN_INTERFACE("RenderModels", "004")
GEN_INTERFACE("RenderModels", "005")
GEN_INTERFACE("RenderModels", "006")

#include "generated/GVRRenderModels.gen.h"

bool CVRRenderModels_002::LoadRenderModel(const char* pchRenderModelName, vr::IVRRenderModels_002::RenderModel_t** ppRenderModel)
{
	return base->LoadRenderModel_Async(pchRenderModelName, (OOVR_RenderModel_t**)ppRenderModel);
}

bool CVRRenderModels_002::LoadTexture(vr::IVRRenderModels_002::TextureID_t textureId, vr::IVRRenderModels_002::RenderModel_TextureMap_t** ppTexture)
{
	return base->LoadTexture_Async((OOVR_TextureID_t)textureId, (OOVR_RenderModel_TextureMap_t**)ppTexture);
}

bool CVRRenderModels_002::GetComponentState(const char* pchRenderModelName, const char* pchComponentName, const vr::VRControllerState_t* pControllerState, vr::IVRRenderModels_002::RenderModel_ComponentState_t* pComponentState)
{
	return base->GetComponentState(pchRenderModelName, pchComponentName, pControllerState, nullptr, (OOVR_RenderModel_ComponentState_t*)pComponentState);
}

// IVRRenderModels_001 (SDK 0.9.2 to 0.9.11): the whole model in one call, its texture inline, as IVRSystem_003 had it
bool CVRRenderModels_001::LoadRenderModel(const char* pchRenderModelName, vr::IVRRenderModels_001::RenderModel_t* pRenderModel)
{
	return base->LoadRenderModelInline(pchRenderModelName, pRenderModel);
}

void CVRRenderModels_001::FreeRenderModel(vr::IVRRenderModels_001::RenderModel_t* pRenderModel)
{
	base->FreeRenderModelInline(pRenderModel);
}

bool CVRRenderModels_001::GetComponentState(const char* pchRenderModelName, const char* pchComponentName,
    vr::VRControllerState_t controllerState, vr::IVRRenderModels_001::ComponentState_t* pComponentState)
{
	OOVR_RenderModel_ComponentState_t state = {};
	bool ok = base->GetComponentState(pchRenderModelName, pchComponentName, &controllerState, nullptr, &state);
	if (pComponentState) {
		pComponentState->mTrackingToComponentRenderModel = state.mTrackingToComponentRenderModel;
		pComponentState->mTrackingToComponentLocal = state.mTrackingToComponentLocal;
		pComponentState->bIsStatic = (state.uProperties & vr::VRComponentProperty_IsStatic) != 0;
		pComponentState->bIsVisible = (state.uProperties & vr::VRComponentProperty_IsVisible) != 0;
	}
	return ok;
}
