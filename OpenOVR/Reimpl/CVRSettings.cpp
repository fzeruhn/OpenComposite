#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("Settings", "001")
GEN_INTERFACE("Settings", "002")
GEN_INTERFACE("Settings", "003")

#include "generated/GVRSettings.gen.h"

typedef BaseSettings::EVRSettingsError EVRSettingsErrorBase;
typedef vr::IVRSettings_001::EVRSettingsError EVRSettingsError001;

// IVRSettings_001 takes the default itself: an unset setting answers it, with no error
static bool UseDefault(EVRSettingsErrorBase error)
{
	return error == VRSettingsError_UnsetSettingHasNoDefault;
}

// Changed in place (split_headers.py's KNOWN_INPLACE_CHANGES): before 0.9.17 it was Sync(peError), whose pointer arrives
// here as bForce, and peError is then whatever is in the next argument's place: never written
bool CVRSettings_001::Sync(bool bForce, EVRSettingsError001* peError)
{
	return base->Sync(bForce, nullptr);
}

bool CVRSettings_001::GetBool(const char* pchSection, const char* pchSettingsKey, bool bDefaultValue, EVRSettingsError001* peError)
{
	EVRSettingsErrorBase error = VRSettingsError_None;
	bool value = base->GetBool(pchSection, pchSettingsKey, &error);
	if (UseDefault(error)) {
		value = bDefaultValue;
		error = VRSettingsError_None;
	}
	if (peError)
		*peError = (EVRSettingsError001)error;
	return value;
}
int32_t CVRSettings_001::GetInt32(const char* pchSection, const char* pchSettingsKey, int32_t nDefaultValue, EVRSettingsError001* peError)
{
	EVRSettingsErrorBase error = VRSettingsError_None;
	int32_t value = base->GetInt32(pchSection, pchSettingsKey, &error);
	if (UseDefault(error)) {
		value = nDefaultValue;
		error = VRSettingsError_None;
	}
	if (peError)
		*peError = (EVRSettingsError001)error;
	return value;
}

float CVRSettings_001::GetFloat(const char* pchSection, const char* pchSettingsKey, float flDefaultValue, EVRSettingsError001* peError)
{
	EVRSettingsErrorBase error = VRSettingsError_None;
	float value = base->GetFloat(pchSection, pchSettingsKey, &error);
	if (UseDefault(error)) {
		value = flDefaultValue;
		error = VRSettingsError_None;
	}
	if (peError)
		*peError = (EVRSettingsError001)error;
	return value;
}

void CVRSettings_001::GetString(const char* pchSection, const char* pchSettingsKey, char* pchValue, uint32_t unValueLen,
    const char* pchDefaultValue, EVRSettingsError001* peError)
{
	EVRSettingsErrorBase error = VRSettingsError_None;
	base->GetString(pchSection, pchSettingsKey, pchValue, unValueLen, &error);
	if (UseDefault(error)) {
		error = VRSettingsError_None;
		std::string fallback = pchDefaultValue ? pchDefaultValue : "";
		if (pchValue && unValueLen) {
			size_t n = std::min<size_t>(fallback.size(), unValueLen - 1);
			memcpy(pchValue, fallback.data(), n);
			pchValue[n] = 0;
		}
		if (unValueLen < fallback.size() + 1)
			error = VRSettingsError_ReadFailed;
	}
	if (peError)
		*peError = (EVRSettingsError001)error;
}
