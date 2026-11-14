#pragma once
#include "BaseCommon.h"

#include "json/json.h"

#include <optional>
#include <string>

class SharedStore;

enum OOVR_EVRSettingsError {
	VRSettingsError_None = 0,
	VRSettingsError_IPCFailed = 1,
	VRSettingsError_WriteFailed = 2,
	VRSettingsError_ReadFailed = 3,
	VRSettingsError_JsonParseFailed = 4,
	VRSettingsError_UnsetSettingHasNoDefault = 5, // This will be returned if the setting does not appear in the appropriate default file and has not been set
};

class BaseSettings {
public:
	typedef OOVR_EVRSettingsError EVRSettingsError;

	/** Settings kept in the process's store (SharedStore::Get()) */
	BaseSettings() = default;
	/** Settings kept in that store (tests) */
	explicit BaseSettings(SharedStore& store) : store(&store) {}

	const char* GetSettingsErrorNameFromEnum(EVRSettingsError eError);

	// Returns true if file sync occurred (force or settings dirty)
	bool Sync(bool bForce = false, EVRSettingsError* peError = nullptr);

	void SetBool(const char* pchSection, const char* pchSettingsKey, bool bValue, EVRSettingsError* peError = nullptr);
	void SetInt32(const char* pchSection, const char* pchSettingsKey, int32_t nValue, EVRSettingsError* peError = nullptr);
	void SetFloat(const char* pchSection, const char* pchSettingsKey, float flValue, EVRSettingsError* peError = nullptr);
	void SetString(const char* pchSection, const char* pchSettingsKey, const char* pchValue, EVRSettingsError* peError = nullptr);

	// Users of the system need to provide a proper default in default.vrsettings in the resources/settings/ directory
	// of either the runtime or the driver_xxx directory. Otherwise the default will be false, 0, 0.0 or ""
	bool GetBool(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError = nullptr);
	int32_t GetInt32(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError = nullptr);
	float GetFloat(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError = nullptr);
	void GetString(const char* pchSection, const char* pchSettingsKey, VR_OUT_STRING() char* pchValue, uint32_t unValueLen, EVRSettingsError* peError = nullptr);

	void RemoveSection(const char* pchSection, EVRSettingsError* peError = nullptr);
	void RemoveKeyInSection(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError = nullptr);

	/** The scale the recommended render target size is multiplied by: steamvr.supersampleScale from the store, else
	 * from the user's steamvr.vrsettings when its supersampleManualOverride is on, else iniRatio (opencomposite.ini's
	 * supersampleRatio); then steam.app.<SteamAppId>.resolutionScale (percent) from the store or that file. */
	static float RenderScale(SharedStore& store, float iniRatio);
	/** RenderScale without the app's own resolutionScale */
	static float GlobalRenderScale(SharedStore& store, float iniRatio);

	/** A setting's value as Get* look it up: the store, OpenComposite's live answers, SteamVR's files, then the
	 * answers OpenComposite always gave; nothing if none has it */
	std::optional<Json::Value> Lookup(const std::string& section, const std::string& key);

private:
	SharedStore& Store();
	void Set(const char* pchSection, const char* pchSettingsKey, const Json::Value& value, EVRSettingsError* peError);

	SharedStore* store = nullptr;
};
