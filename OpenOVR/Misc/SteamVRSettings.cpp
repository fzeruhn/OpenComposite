#include "stdafx.h"

#include "SteamVRSettings.h"

#include "Misc/JsonFile.h"
#include "Misc/SteamVRPaths.h"

#include <chrono>
#include <mutex>
#include <system_error>

namespace fs = std::filesystem;

namespace {

std::string Utf8(const fs::path& p)
{
	std::u8string s = p.u8string();
	return std::string(s.begin(), s.end());
}

// One settings file: read again when its modification time changes, looked at no more than once a second
struct SettingsFile {
	fs::path path;
	bool hasPath = false;
	fs::file_time_type modified{};
	bool exists = false;
	Json::Value data{ Json::objectValue };
	std::chrono::steady_clock::time_point checked{};
	bool everChecked = false;

	void SetPath(std::optional<fs::path> p)
	{
		hasPath = p.has_value();
		path = p.value_or(fs::path());
		exists = false;
		data = Json::Value(Json::objectValue);
		everChecked = false;
	}

	void Refresh()
	{
		auto now = std::chrono::steady_clock::now();
		if (everChecked && now - checked < std::chrono::seconds(1))
			return;
		everChecked = true;
		checked = now;

		std::error_code ec;
		if (!hasPath || !fs::is_regular_file(path, ec)) {
			exists = false;
			data = Json::Value(Json::objectValue);
			return;
		}
		fs::file_time_type time = fs::last_write_time(path, ec);
		if (ec || (exists && time == modified))
			return;

		Json::Value read;
		std::string error;
		if (ReadJsonFileLenient(path, read, error) && read.isObject()) {
			data = read;
		} else {
			OOVR_LOGF("SteamVR's settings file %s can't be read, ignored: %s", Utf8(path).c_str(), error.c_str());
			data = Json::Value(Json::objectValue);
		}
		exists = true;
		modified = time;
	}

	std::optional<Json::Value> Value(const std::string& section, const std::string& key) const
	{
		const Json::Value* s = data.find(section.data(), section.data() + section.size());
		if (!s || !s->isObject())
			return std::nullopt;
		const Json::Value* v = s->find(key.data(), key.data() + key.size());
		if (!v || v->isNull())
			return std::nullopt;
		return *v;
	}
};

struct Files {
	std::mutex lock;
	SettingsFile user, defaults;
	bool found = false;

	void FindOnce()
	{
		if (found)
			return;
		found = true;
		std::optional<fs::path> config = FindSteamConfigDir();
		user.SetPath(config ? std::optional<fs::path>(*config / "steamvr.vrsettings") : std::nullopt);
		std::optional<fs::path> steamvr = FindSteamVRInstall();
		defaults.SetPath(steamvr ? std::optional<fs::path>(*steamvr / "resources" / "settings" / "default.vrsettings") : std::nullopt);
	}
};

Files& TheFiles()
{
	static Files files;
	return files;
}

} // namespace

std::optional<Json::Value> SteamVRSettingValue(const std::string& section, const std::string& key)
{
	Files& f = TheFiles();
	std::lock_guard guard(f.lock);
	f.FindOnce();
	f.user.Refresh();
	if (auto v = f.user.Value(section, key))
		return v;
	f.defaults.Refresh();
	return f.defaults.Value(section, key);
}

std::optional<Json::Value> SteamVRUserSettingValue(const std::string& section, const std::string& key)
{
	Files& f = TheFiles();
	std::lock_guard guard(f.lock);
	f.FindOnce();
	f.user.Refresh();
	return f.user.Value(section, key);
}

void SteamVRSettingsUseFiles(std::optional<fs::path> userFile, std::optional<fs::path> defaultFile)
{
	Files& f = TheFiles();
	std::lock_guard guard(f.lock);
	if (!userFile && !defaultFile) {
		f.found = false;
		return;
	}
	f.found = true;
	f.user.SetPath(userFile);
	f.defaults.SetPath(defaultFile);
}
