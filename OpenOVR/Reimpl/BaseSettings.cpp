#include "stdafx.h"
#include "Misc/EnumNames.h"
#define BASE_IMPL
#include "BaseSettings.h"
#include "BaseSystem.h"
#include "Misc/Config.h"
#include "generated/interfaces/IVRSettings_001.h"
#include "generated/interfaces/IVRSettings_002.h"
#include <string>

#ifndef OC_XR_PORT
// Required for the Oculus audio switching thing
#include <codecvt>
#endif

// A setting OpenComposite has no value for: counted in the compatibility summary (with the first one's name), and
// the caller goes on to return its "not set" answer
#define UNSET_SETTING()                                                                               \
	do {                                                                                              \
		OC_SOFT_SITE_;                                                                                \
		oc_soft_fail(&oc_soft_site_, "Setting %s.%s isn't set", pchSection ? pchSection : "(null)", \
		    pchSettingsKey ? pchSettingsKey : "(null)");                                              \
	} while (0)

namespace kk1 = vr::IVRSettings_001;
namespace kk = vr::IVRSettings_002;

const char* BaseSettings::GetSettingsErrorNameFromEnum(EVRSettingsError eError)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRSettingsError", (int32_t)eError, "Unknown VRSettingsError (%d)");
}
bool BaseSettings::Sync(bool bForce, EVRSettingsError* peError)
{
	// We update everything immmediately, so AFAIK this shouldn't do anything?

	if (peError)
		*peError = VRSettingsError_None;

	return false;
}
void BaseSettings::SetBool(const char* pchSection, const char* pchSettingsKey, bool bValue, EVRSettingsError* peError)
{
	// A null section or key reads as unset and doesn't write
	if (!pchSection || !pchSettingsKey) {
		if (peError)
			*peError = VRSettingsError_WriteFailed;
		return;
	}
	if (peError)
		*peError = VRSettingsError_None;

	string section = pchSection;
	string key = pchSettingsKey;
	if (section == kk::k_pch_SteamVR_Section) {
		if (key == kk::k_pch_SteamVR_ShowStage_Bool) {
			// AFAIK we can just ignore this, it shows a box on the floor.
			return;
		} else if (key == kk::k_pch_SteamVR_ForceFadeOnBadTracking_Bool) {
			// Ignore this, it's very unlikely the user will have tracking problems inside their guardian boundries.
			return;
		} else if (key == kk::k_pch_SteamVR_BackgroundUseDomeProjection_Bool) {
			// We don't have overlay (which == background?) support
			return;
		}
	} else if (section == kk::k_pch_Notifications_Section) {
		if (key == kk::k_pch_Notifications_DoNotDisturb_Bool) {
			// AFAIK there's no way to handle this with LibOVR
			return;
		}
	} else if (section == kk::k_pch_CollisionBounds_Section) {
		// No way to alter guardian from LibOVR
		if (key == kk::k_pch_CollisionBounds_GroundPerimeterOn_Bool) {
			return;
		} else if (key == kk::k_pch_CollisionBounds_CenterMarkerOn_Bool) {
			return;
		}
	} else if (section == kk::k_pch_Dashboard_Section) {
		if (key == kk::k_pch_Dashboard_EnableDashboard_Bool) {
			// OpenOVR doesn't have a dashboard
			return;
		}
	}

	if (peError)
		*peError = VRSettingsError_WriteFailed;

	UNSET_SETTING();
}
void BaseSettings::SetInt32(const char* pchSection, const char* pchSettingsKey, int32_t nValue, EVRSettingsError* peError)
{
	// A null section or key reads as unset and doesn't write
	if (!pchSection || !pchSettingsKey) {
		if (peError)
			*peError = VRSettingsError_WriteFailed;
		return;
	}
	if (peError)
		*peError = VRSettingsError_None;

	string section = pchSection;
	string key = pchSettingsKey;
	if (section == kk::k_pch_CollisionBounds_Section) {
		if (key == kk::k_pch_CollisionBounds_ColorGammaA_Int32) {
			// AFAIK you can't change the opacity of Guardian, however you can change it's colour
			return;
		}
	}

	if (peError)
		*peError = VRSettingsError_WriteFailed;

	UNSET_SETTING();
}
void BaseSettings::SetFloat(const char* pchSection, const char* pchSettingsKey, float flValue, EVRSettingsError* peError)
{
	// A null section or key reads as unset and doesn't write
	if (!pchSection || !pchSettingsKey) {
		if (peError)
			*peError = VRSettingsError_WriteFailed;
		return;
	}
	if (peError)
		*peError = VRSettingsError_None;

	string section = pchSection;
	string key = pchSettingsKey;
	if (section == kk::k_pch_CollisionBounds_Section) {
		if (key == kk::k_pch_CollisionBounds_FadeDistance_Float) {
			// No way to alter guardian from LibOVR
			return;
		}
	}
	if (section == kk::k_pch_SteamVR_Section) {
		if (key == kk::k_pch_SteamVR_IpdOffset_Float) {
			OOVR_LOGF("Warning: Unsupported key - SetFloat %s %s %f", pchSection, pchSettingsKey, flValue);
			return;
		}
	}

	if (peError)
		*peError = VRSettingsError_WriteFailed;

	UNSET_SETTING();
}
void BaseSettings::SetString(const char* pchSection, const char* pchSettingsKey, const char* pchValue, EVRSettingsError* peError)
{
	// A null section or key reads as unset and doesn't write
	if (!pchSection || !pchSettingsKey) {
		if (peError)
			*peError = VRSettingsError_WriteFailed;
		return;
	}
	if (peError)
		*peError = VRSettingsError_None;

	//*** Implementation here ***//

	if (peError)
		*peError = VRSettingsError_WriteFailed;

	UNSET_SETTING();
}
bool BaseSettings::GetBool(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError)
{
	// A null section or key reads as unset and doesn't write
	if (!pchSection || !pchSettingsKey) {
		if (peError)
			*peError = VRSettingsError_ReadFailed;
		return false;
	}
	string section = pchSection;
	string key = pchSettingsKey;

	if (peError)
		*peError = VRSettingsError_None;

	if (section == kk::k_pch_SteamVR_Section) {
		if (key == kk::k_pch_SteamVR_UsingSpeakers_Bool) {
			// True if the user is using external speakers (not attached to
			// their head), and the sound should thus be adjusted.
			// Note when set to true, expect k_pch_SteamVR_SpeakersForwardYawOffsetDegrees_Float
			return false; // TODO
		} else if (key == kk1::k_pch_SteamVR_DirectMode_Bool) {
			// Note that this is NOT the same as k_pch_DirectMode_Section - the key is very slightly different
			// direct_mode vs directMode

			// Oculus doesn't support windowed mode
			return true;
		} else if (key == kk::k_pch_SteamVR_RetailDemo_Bool) {
			// What? (Used in The Lab btw)
			return false;
		} else if (key == kk1::k_pch_SteamVR_AllowReprojection_Bool || key == "allowInterleavedReprojection") {
			// There were two different reprojection strings for the same property - allowReprojection and allowInterleavedReprojection, however the key was removed
			// at some point, so it's currently just specified as a string. TODO modify the header splitter to keep these old properties around somewhere.
			return true;
		}
	}

	if (peError)
		*peError = VRSettingsError_ReadFailed;
	UNSET_SETTING();

	return false;
}
int32_t BaseSettings::GetInt32(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError)
{
	// A null section or key reads as unset and doesn't write
	if (!pchSection || !pchSettingsKey) {
		if (peError)
			*peError = VRSettingsError_ReadFailed;
		return 0;
	}
	if (peError)
		*peError = VRSettingsError_None;

	if (peError)
		*peError = VRSettingsError_ReadFailed;
	UNSET_SETTING();

	return 0;
}
float BaseSettings::GetFloat(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError)
{
	// A null section or key reads as unset and doesn't write
	if (!pchSection || !pchSettingsKey) {
		if (peError)
			*peError = VRSettingsError_ReadFailed;
		return 0.0f;
	}
	string section = pchSection;
	string key = pchSettingsKey;

	if (peError)
		*peError = VRSettingsError_None;

	if (section == kk::k_pch_SteamVR_Section) {
		if (key == kk::k_pch_SteamVR_SupersampleScale_Float || key == kk1::k_pch_SteamVR_RenderTargetMultiplier_Float) {
			return oovr_global_configuration.SupersampleRatio();
		} else if (key == kk::k_pch_SteamVR_IPD_Float) {
			return BaseSystem::SGetIpd();
		} else if (key == kk::k_pch_SteamVR_IpdOffset_Float) {
			return 0.0f;
		}
	} else if (section == kk::k_pch_CollisionBounds_Section) {
		if (key == kk::k_pch_CollisionBounds_FadeDistance_Float) {
			return 1.0f; // made up some value that we will not use
		}
	} else if (section.find("steam.app") == 0) {
		if (key == "resolutionScale")
			return 100.0f;
	}

	if (peError)
		*peError = VRSettingsError_ReadFailed;
	UNSET_SETTING();
	return 1.0f;
}
void BaseSettings::GetString(const char* pchSection, const char* pchSettingsKey, VR_OUT_STRING() char* pchValue,
    uint32_t unValueLen, EVRSettingsError* peError)
{
	// A null section or key reads as unset and doesn't write
	if (!pchSection || !pchSettingsKey) {
		if (peError)
			*peError = VRSettingsError_ReadFailed;
		if (pchValue && unValueLen)
			pchValue[0] = 0;
		return;
	}

	if (peError)
		*peError = VRSettingsError_None;

	string section = pchSection;
	string key = pchSettingsKey;

	string result;

	if (section == kk::k_pch_SteamVR_Section) {
		if (key == kk::k_pch_SteamVR_GridColor_String) {
			// When I tested it under SteamVR, it did actually just return an empty string
			result = "";
			goto found;
		}
	} else if (section == kk::k_pch_LastKnown_Section) {
		if (key == kk::k_pch_LastKnown_HMDModel_String) {
			result = "Oculus Quest2";
			goto found;
		} else if (key == kk::k_pch_LastKnown_HMDManufacturer_String) {
			result = "Oculus";
			goto found;
		}
	} else if (section == kk::k_pch_audio_Section) {
#ifdef OC_XR_PORT
		OOVR_SOFT_ABORT("k_pch_audio_Section unimplemented");
		result = "";
		goto found;
#else
		// Sansar, and hopefully other games (since this very nicely solves the audio device problem), uses the
		//  auto-switching SteamVR audio devices.
		// See https://gitlab.com/znixian/OpenOVR/issues/65

		wstring_convert<codecvt_utf8<wchar_t>> conv;

		if (key == kk1::k_pch_audio_OnPlaybackDevice_String) {
			wchar_t buff[OVR_AUDIO_MAX_DEVICE_STR_SIZE];
			ovr_GetAudioDeviceOutGuidStr(buff);
			result = conv.to_bytes(buff);
			goto found;
		} else if (key == kk1::k_pch_audio_OnRecordDevice_String) {
			wchar_t buff[OVR_AUDIO_MAX_DEVICE_STR_SIZE];
			ovr_GetAudioDeviceInGuidStr(buff);
			result = conv.to_bytes(buff);
			goto found;
		}
#endif
	}

	if (peError)
		*peError = VRSettingsError_ReadFailed;
	UNSET_SETTING();

	return;

found:

	// Cut short and terminated if the app's buffer is too small (+1 for the null)
	if (pchValue && unValueLen) {
		size_t n = std::min<size_t>(result.size(), unValueLen - 1);
		memcpy(pchValue, result.data(), n);
		pchValue[n] = 0;
	}
	if (unValueLen < result.length() + 1 && peError)
		*peError = VRSettingsError_ReadFailed;
}
void BaseSettings::RemoveSection(const char* pchSection, EVRSettingsError* peError)
{
	// There are no stored settings yet, so there's nothing to remove
	if (peError)
		*peError = VRSettingsError_WriteFailed;
	OC_UNSUPPORTED_VOID("RemoveSection: settings aren't stored");
}
void BaseSettings::RemoveKeyInSection(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError)
{
	if (peError)
		*peError = VRSettingsError_WriteFailed;
	OC_UNSUPPORTED_VOID("RemoveKeyInSection: settings aren't stored");
}
