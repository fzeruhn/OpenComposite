#include "stdafx.h"
#include "Misc/EnumNames.h"
#define BASE_IMPL
#include "BaseSettings.h"
#include "Drivers/Backend.h"
#include "Misc/Config.h"
#include "Misc/Numbers.h"
#include "Misc/SharedStore.h"
#include "Misc/SteamVRSettings.h"
#include "../DrvOpenXR/DeviceIdentity.h"
#include "generated/interfaces/IVRSettings_001.h"
#include "generated/interfaces/IVRSettings_002.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

// Settings live in the store's Settings document, as {"<section>": {"<key>": value}}, shared by every
// OpenComposite program of the user. Gets fall back to OpenComposite's live answers, then SteamVR's own files, then the
// answers OpenComposite always gave.

namespace kk1 = vr::IVRSettings_001;
namespace kk = vr::IVRSettings_002;

namespace {

// A setting OpenComposite has no value for: counted in the compatibility summary (with the first one's name), and
// the caller goes on to return its "not set" answer
void UnsetSetting(const char* pchSection, const char* pchSettingsKey)
{
	OC_SOFT_SITE_;
	oc_soft_fail(&oc_soft_site_, "Setting %s.%s isn't set", pchSection, pchSettingsKey);
}

const Json::Value* Member(const Json::Value& object, const std::string& name)
{
	if (!object.isObject())
		return nullptr;
	return object.find(name.data(), name.data() + name.size());
}

std::optional<Json::Value> StoredValue(SharedStore& store, const std::string& section, const std::string& key)
{
	Json::Value settings = store.Read(StoreDoc::Settings);
	const Json::Value* s = Member(settings, section);
	const Json::Value* v = s ? Member(*s, key) : nullptr;
	if (!v || v->isNull())
		return std::nullopt;
	return *v;
}

// The OpenXR system's name, which picks the HMD's identity row; empty before a session (and in the unit tests)
std::string HmdSystemName()
{
	auto sessionLock = xr_session.lock_shared(); // held while reading: a session restart waits for it
	if (!xr_gbl)
		return {};
	return xr_gbl->systemProperties.systemName;
}

// Text as a number, all of it; nothing for anything else
std::optional<double> ParseNumber(const std::string& text)
{
	size_t used = 0;
	std::optional<double> d = Numbers::ParseDouble(text, &used);
	if (!d || used != text.size())
		return std::nullopt;
	return d;
}

// A stored value as another type, as SteamVR converts them (a float read as a string is "%f"):
// numbers and bools convert both ways, numbers become text, text becomes a number if all of it is one. Nothing if
// it doesn't convert (an object, an array, text that isn't a number).
std::optional<double> AsNumber(const Json::Value& v)
{
	if (v.isBool())
		return v.asBool() ? 1.0 : 0.0;
	if (v.isNumeric())
		return v.asDouble();
	if (v.isString()) {
		std::string s = v.asString();
		if (s == "true")
			return 1.0;
		if (s == "false")
			return 0.0;
		return ParseNumber(s);
	}
	return std::nullopt;
}

std::optional<std::string> AsString(const Json::Value& v)
{
	if (v.isString())
		return v.asString();
	if (v.isBool())
		return v.asBool() ? "true" : "false";
	if (v.type() == Json::realValue) { // before isInt64, which a whole float also is
		return Numbers::FormatFixed(v.asDouble(), 6); // as "%f" in the C locale
	}
	if (v.isInt64())
		return std::to_string(v.asInt64());
	if (v.isUInt64())
		return std::to_string(v.asUInt64());
	return std::nullopt;
}

int32_t ToInt32(double d)
{
	if (std::isnan(d))
		return 0;
	if (d >= (double)std::numeric_limits<int32_t>::max())
		return std::numeric_limits<int32_t>::max();
	if (d <= (double)std::numeric_limits<int32_t>::min())
		return std::numeric_limits<int32_t>::min();
	return (int32_t)d;
}

void SetError(OOVR_EVRSettingsError* peError, OOVR_EVRSettingsError error)
{
	if (peError)
		*peError = error;
}

} // namespace

SharedStore& BaseSettings::Store()
{
	return store ? *store : SharedStore::Get();
}

const char* BaseSettings::GetSettingsErrorNameFromEnum(EVRSettingsError eError)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRSettingsError", (int32_t)eError, "Unknown VRSettingsError (%d)");
}

bool BaseSettings::Sync(bool bForce, EVRSettingsError* peError)
{
	// Every Set is written to the store at once. SteamVR answers whether it wrote its file, which a forced sync always
	// does
	SetError(peError, VRSettingsError_None);
	return bForce;
}

std::optional<Json::Value> BaseSettings::Lookup(const std::string& section, const std::string& key)
{
	// 1. What a program set
	if (auto v = StoredValue(Store(), section, key))
		return v;

	// 2. OpenComposite's live answers
	if (section == kk::k_pch_SteamVR_Section) {
		// supersampleScale as SteamVR keeps it, scaling the pixel count (the app's resolutionScale has its own key);
		// renderTargetMultiplier per axis
		if (key == kk::k_pch_SteamVR_SupersampleScale_Float) {
			float axis = GlobalRenderScale(Store(), oovr_global_configuration.SupersampleRatio());
			return Json::Value(axis * axis);
		}
		if (key == kk1::k_pch_SteamVR_RenderTargetMultiplier_Float)
			return Json::Value(RenderScale(Store(), oovr_global_configuration.SupersampleRatio()));
		if (key == kk::k_pch_SteamVR_IPD_Float) {
			// The HMD's, once OpenComposite has one
			if (BackendManager* backends = BackendManager::InstancePtr()) {
				if (std::shared_ptr<IHMD> hmd = backends->GetPrimaryHMD())
					return Json::Value(hmd->GetIPD());
			}
		}
		if (key == kk::k_pch_SteamVR_MotionSmoothing_Bool)
			return Json::Value(false); // OpenComposite doesn't make frames up
	} else if (section == kk::k_pch_LastKnown_Section) {
		const std::string systemName = HmdSystemName();
		const DeviceIdentity& hmd = HmdIdentity(systemName);
		if (key == kk::k_pch_LastKnown_HMDManufacturer_String)
			return Json::Value(hmd.manufacturer);
		if (key == kk::k_pch_LastKnown_HMDModel_String) {
			// The row's model number, as Prop_ModelNumber_String answers it; the default row has none and kept the
			// answer OpenComposite always gave
			return Json::Value(hmd.modelNumber.empty() ? std::string("Oculus Quest2") : hmd.modelNumber);
		}
	}

	// 3. SteamVR's own files
	if (auto v = SteamVRSettingValue(section, key))
		return v;

	// 4. The answers OpenComposite always gave
	if (section == kk::k_pch_SteamVR_Section) {
		// True if the user is using external speakers (not attached to their head)
		if (key == kk::k_pch_SteamVR_UsingSpeakers_Bool)
			return Json::Value(false);
		// direct_mode, not the DirectMode section: there's no windowed mode
		if (key == kk1::k_pch_SteamVR_DirectMode_Bool)
			return Json::Value(true);
		if (key == kk::k_pch_SteamVR_RetailDemo_Bool) // The Lab reads it
			return Json::Value(false);
		// allowInterleavedReprojection was the key's older name, gone from the headers
		if (key == kk1::k_pch_SteamVR_AllowReprojection_Bool || key == "allowInterleavedReprojection")
			return Json::Value(true);
		if (key == kk::k_pch_SteamVR_IpdOffset_Float)
			return Json::Value(0.0);
		// Under SteamVR it was an empty string
		if (key == kk::k_pch_SteamVR_GridColor_String)
			return Json::Value("");
	} else if (section == kk::k_pch_CollisionBounds_Section) {
		if (key == kk::k_pch_CollisionBounds_FadeDistance_Float)
			return Json::Value(1.0); // made up, and not used
	} else if (section.rfind("steam.app.", 0) == 0) {
		if (key == "resolutionScale")
			return Json::Value(100.0);
	} else if (section == kk::k_pch_audio_Section) {
		// SteamVR's audio device switching (Sansar reads it): OpenXR has no audio devices
		if (key == kk1::k_pch_audio_OnPlaybackDevice_String || key == kk1::k_pch_audio_OnRecordDevice_String) {
			OOVR_SOFT_ABORT("k_pch_audio_Section unimplemented");
			return Json::Value("");
		}
	}

	return std::nullopt;
}

float BaseSettings::GlobalRenderScale(SharedStore& store, float iniRatio)
{
	// The ini's ratio scales each axis. SteamVR's supersampleScale and resolutionScale scale the pixel count, so each
	// axis takes their square root.
	float scale = iniRatio;
	if (auto v = StoredValue(store, kk::k_pch_SteamVR_Section, kk::k_pch_SteamVR_SupersampleScale_Float); v && AsNumber(*v)) {
		scale = (float)std::sqrt(*AsNumber(*v));
	} else if (auto manual = SteamVRUserSettingValue(kk::k_pch_SteamVR_Section, kk::k_pch_SteamVR_SupersampleManualOverride_Bool);
	           manual && AsNumber(*manual).value_or(0) != 0) {
		// Without the manual override, SteamVR picks the scale itself and the file's is its last pick, not the user's
		if (auto file = SteamVRUserSettingValue(kk::k_pch_SteamVR_Section, kk::k_pch_SteamVR_SupersampleScale_Float); file && AsNumber(*file))
			scale = (float)std::sqrt(*AsNumber(*file));
	}
	if (!(scale > 0.0f) || scale > 16.0f)
		return iniRatio;
	return scale;
}

float BaseSettings::RenderScale(SharedStore& store, float iniRatio)
{
	float scale = GlobalRenderScale(store, iniRatio);
	const char* appId = getenv("SteamAppId");
	if (appId && appId[0]) {
		std::string section = std::string("steam.app.") + appId;
		std::optional<Json::Value> percent = StoredValue(store, section, "resolutionScale");
		if (!percent)
			percent = SteamVRUserSettingValue(section, "resolutionScale");
		if (percent && AsNumber(*percent))
			scale *= (float)std::sqrt(*AsNumber(*percent) / 100.0);
	}

	// A scale that would make no image, or a huge one, isn't one
	if (!(scale > 0.0f) || scale > 16.0f)
		return iniRatio;
	return scale;
}

void BaseSettings::Set(const char* pchSection, const char* pchSettingsKey, const Json::Value& value, EVRSettingsError* peError)
{
	// A null section or key doesn't write
	if (!pchSection || !pchSettingsKey) {
		SetError(peError, VRSettingsError_WriteFailed);
		return;
	}
	std::string section = pchSection, key = pchSettingsKey;
	bool written = Store().Update(StoreDoc::Settings, [&](Json::Value& data) {
		if (!data.isObject())
			data = Json::Value(Json::objectValue);
		if (!data[section].isObject())
			data[section] = Json::Value(Json::objectValue);
		data[section][key] = value;
	});
	SetError(peError, written ? VRSettingsError_None : VRSettingsError_WriteFailed);
}

void BaseSettings::SetBool(const char* pchSection, const char* pchSettingsKey, bool bValue, EVRSettingsError* peError)
{
	Set(pchSection, pchSettingsKey, Json::Value(bValue), peError);
}
void BaseSettings::SetInt32(const char* pchSection, const char* pchSettingsKey, int32_t nValue, EVRSettingsError* peError)
{
	Set(pchSection, pchSettingsKey, Json::Value(nValue), peError);
}
void BaseSettings::SetFloat(const char* pchSection, const char* pchSettingsKey, float flValue, EVRSettingsError* peError)
{
	// JSON has no NaN or infinity
	if (!std::isfinite(flValue)) {
		SetError(peError, VRSettingsError_WriteFailed);
		return;
	}
	Set(pchSection, pchSettingsKey, Json::Value((double)flValue), peError);
}
void BaseSettings::SetString(const char* pchSection, const char* pchSettingsKey, const char* pchValue, EVRSettingsError* peError)
{
	if (!pchValue) {
		SetError(peError, VRSettingsError_WriteFailed);
		return;
	}
	Set(pchSection, pchSettingsKey, Json::Value(pchValue), peError);
}

bool BaseSettings::GetBool(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError)
{
	// A null section or key reads as unset
	if (!pchSection || !pchSettingsKey) {
		SetError(peError, VRSettingsError_ReadFailed);
		return false;
	}
	std::optional<Json::Value> v = Lookup(pchSection, pchSettingsKey);
	if (!v) {
		SetError(peError, VRSettingsError_UnsetSettingHasNoDefault);
		UnsetSetting(pchSection, pchSettingsKey);
		return false;
	}
	std::optional<double> n = AsNumber(*v);
	SetError(peError, n ? VRSettingsError_None : VRSettingsError_ReadFailed);
	return n && *n != 0;
}

int32_t BaseSettings::GetInt32(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError)
{
	if (!pchSection || !pchSettingsKey) {
		SetError(peError, VRSettingsError_ReadFailed);
		return 0;
	}
	std::optional<Json::Value> v = Lookup(pchSection, pchSettingsKey);
	if (!v) {
		SetError(peError, VRSettingsError_UnsetSettingHasNoDefault);
		UnsetSetting(pchSection, pchSettingsKey);
		return 0;
	}
	std::optional<double> n = AsNumber(*v);
	SetError(peError, n ? VRSettingsError_None : VRSettingsError_ReadFailed);
	return n ? ToInt32(*n) : 0;
}

float BaseSettings::GetFloat(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError)
{
	if (!pchSection || !pchSettingsKey) {
		SetError(peError, VRSettingsError_ReadFailed);
		return 0.0f;
	}
	std::optional<Json::Value> v = Lookup(pchSection, pchSettingsKey);
	if (!v) {
		SetError(peError, VRSettingsError_UnsetSettingHasNoDefault);
		UnsetSetting(pchSection, pchSettingsKey);
		return 0.0f;
	}
	std::optional<double> n = AsNumber(*v);
	SetError(peError, n ? VRSettingsError_None : VRSettingsError_ReadFailed);
	return n ? (float)*n : 0.0f;
}

void BaseSettings::GetString(const char* pchSection, const char* pchSettingsKey, VR_OUT_STRING() char* pchValue,
    uint32_t unValueLen, EVRSettingsError* peError)
{
	if (pchValue && unValueLen)
		pchValue[0] = 0;
	if (!pchSection || !pchSettingsKey) {
		SetError(peError, VRSettingsError_ReadFailed);
		return;
	}
	std::optional<Json::Value> v = Lookup(pchSection, pchSettingsKey);
	if (!v) {
		SetError(peError, VRSettingsError_UnsetSettingHasNoDefault);
		UnsetSetting(pchSection, pchSettingsKey);
		return;
	}
	std::optional<std::string> result = AsString(*v);
	if (!result) {
		SetError(peError, VRSettingsError_ReadFailed);
		return;
	}
	SetError(peError, VRSettingsError_None);

	// Cut short and terminated if the app's buffer is too small (+1 for the null)
	if (pchValue && unValueLen) {
		size_t n = std::min<size_t>(result->size(), unValueLen - 1);
		memcpy(pchValue, result->data(), n);
		pchValue[n] = 0;
	}
	if (unValueLen < result->length() + 1)
		SetError(peError, VRSettingsError_ReadFailed);
}

void BaseSettings::RemoveSection(const char* pchSection, EVRSettingsError* peError)
{
	if (!pchSection) {
		SetError(peError, VRSettingsError_WriteFailed);
		return;
	}
	std::string section = pchSection;
	bool written = Store().Update(StoreDoc::Settings, [&](Json::Value& data) {
		if (data.isObject())
			data.removeMember(section);
	});
	SetError(peError, written ? VRSettingsError_None : VRSettingsError_WriteFailed);
}

void BaseSettings::RemoveKeyInSection(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError)
{
	if (!pchSection || !pchSettingsKey) {
		SetError(peError, VRSettingsError_WriteFailed);
		return;
	}
	std::string section = pchSection, key = pchSettingsKey;
	bool written = Store().Update(StoreDoc::Settings, [&](Json::Value& data) {
		// isMember first: [] would add the section
		if (data.isObject() && data.isMember(section) && data[section].isObject()) {
			data[section].removeMember(key);
			if (data[section].empty())
				data.removeMember(section);
		}
	});
	SetError(peError, written ? VRSettingsError_None : VRSettingsError_WriteFailed);
}
