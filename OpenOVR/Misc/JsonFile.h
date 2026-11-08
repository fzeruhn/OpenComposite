#pragma once

// JSON files that several programs share (applist.json, apps-config.json next to the DLL): reading never throws,
// writing never leaves a half-written file behind.

#include "json/json.h"

#include <filesystem>
#include <string>
#include <string_view>

// Reads path into result. A file that's missing, empty, half-written or not a JSON object gives an empty object
// and false (logged unless missing); a good file gives true.
bool oc_read_json_file(const std::filesystem::path& path, Json::Value& result);

// Writes value to path through a temporary file that then replaces it, so another program reading it at the same
// time sees the old file or the new one, never part of one. False (logged) if it couldn't be written.
bool oc_write_json_file(const std::filesystem::path& path, const Json::Value& value);

// Games' own JSON (action manifests, binding files, opencomposite settings) as SteamVR reads it: a UTF-8 BOM,
// // and /* */ comments, trailing commas and CRLF are all fine. False with a readable error for anything else.
bool ParseJsonLenient(std::string_view text, Json::Value& out, std::string& error);

// The same for a file (opened by its wide path on Windows); a missing or unreadable file is an error too
bool ReadJsonFileLenient(const std::filesystem::path& path, Json::Value& out, std::string& error);
