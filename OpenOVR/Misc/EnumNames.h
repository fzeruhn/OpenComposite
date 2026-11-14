#pragma once
// Names of OpenVR enum values, generated from the OpenVR headers by scripts/gen_enum_names.py (EnumNames.gen.cpp).

#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>

// The name of value in the enum named enumType ("EVRInitError", "EVREventType", ...), or nullptr if it has none
const char* OC_EnumName(const char* enumType, int32_t value);

// What SteamVR's *NameFromEnum calls answer (its recording, tests/conformance/expected): the value's name, or for a
// value without one, unknownFormat with the value as its one %d ("Unknown EVREventType (%d)"); knownWithValue
// appends " (<value>)" to a name as well, as GetRenderModelErrorNameFromEnum does. The string stays valid for the
// life of the process.
inline const char* OC_EnumNameAsSteamVR(const char* enumType, int32_t value, const char* unknownFormat, bool knownWithValue = false)
{
	const char* name = OC_EnumName(enumType, value);
	if (name && !knownWithValue)
		return name;

	char buffer[256];
	if (name)
		std::snprintf(buffer, sizeof(buffer), "%s (%d)", name, (int)value);
	else
		std::snprintf(buffer, sizeof(buffer), unknownFormat, (int)value);

	// Kept so the pointer stays good; a program asking for every int doesn't grow it past the limit
	static std::mutex lock;
	static std::map<std::string, std::string> strings;
	std::lock_guard<std::mutex> guard(lock);
	auto it = strings.find(buffer);
	if (it != strings.end())
		return it->second.c_str();
	if (strings.size() >= 4096)
		return "Unknown";
	return strings.emplace(buffer, buffer).first->second.c_str();
}
