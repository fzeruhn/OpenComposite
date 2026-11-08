#include "stdafx.h"

#include "JsonFile.h"

#include "logging.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// For log lines: string() can throw on Windows for characters outside the ANSI code page
static std::string Utf8(const fs::path& p)
{
	std::u8string u = p.u8string();
	return std::string(u.begin(), u.end());
}

bool oc_read_json_file(const fs::path& path, Json::Value& result)
{
	result = Json::Value(Json::objectValue);
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;

	Json::CharReaderBuilder builder;
	Json::Value parsed;
	std::string errors;
	if (!Json::parseFromStream(builder, in, &parsed, &errors)) {
		OOVR_LOGF("%s isn't valid JSON (another program may be writing it), treating it as empty: %s",
		    Utf8(path).c_str(), errors.c_str());
		return false;
	}
	if (!parsed.isObject()) {
		OOVR_LOGF("%s isn't a JSON object, treating it as empty", Utf8(path).c_str());
		return false;
	}
	result = std::move(parsed);
	return true;
}

bool oc_write_json_file(const fs::path& path, const Json::Value& value)
{
#ifdef _WIN32
	unsigned long pid = GetCurrentProcessId();
#else
	unsigned long pid = (unsigned long)getpid();
#endif
	// One temporary file per process and thread, in the same folder so the rename doesn't cross drives
	fs::path temp = path;
	temp += ".tmp" + std::to_string(pid) + "-" + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()));

	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out) {
			OOVR_LOGF("Couldn't write %s: %s", Utf8(temp).c_str(), strerror(errno));
			return false;
		}
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "\t";
		std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
		writer->write(value, &out);
		out.flush();
		if (!out) {
			OOVR_LOGF("Couldn't write %s", Utf8(temp).c_str());
			out.close();
			std::error_code ignored;
			fs::remove(temp, ignored);
			return false;
		}
	}

	// Replacing fails for a moment while another program has the file open (Windows), so try a few times
	std::error_code ec;
	for (int attempt = 0; attempt < 20; attempt++) {
		fs::rename(temp, path, ec);
		if (!ec)
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	OOVR_LOGF("Couldn't replace %s: %s", Utf8(path).c_str(), ec.message().c_str());
	std::error_code ignored;
	fs::remove(temp, ignored);
	return false;
}
