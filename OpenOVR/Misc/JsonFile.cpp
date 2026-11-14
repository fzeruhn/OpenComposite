#include "stdafx.h"

#include "JsonFile.h"

#include "logging.h"

#include <cctype>
#include <chrono>
#include <memory>
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
	bool ok;
	try {
		ok = Json::parseFromStream(builder, in, &parsed, &errors);
	} catch (const std::exception& e) {
		// jsoncpp throws past its nesting limit
		ok = false;
		errors = e.what();
	}
	if (!ok) {
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

// Removes the commas that jsoncpp won't take: those followed (past whitespace and comments) by } or ]. Strings
// and comments are copied as they are.
static std::string DropTrailingCommas(std::string_view in)
{
	std::string out;
	out.reserve(in.size());
	size_t i = 0;
	auto skipSpaceAndComments = [&](size_t j) {
		while (j < in.size()) {
			if (isspace((unsigned char)in[j])) {
				j++;
			} else if (in.compare(j, 2, "//") == 0) {
				while (j < in.size() && in[j] != '\n')
					j++;
			} else if (in.compare(j, 2, "/*") == 0) {
				size_t end = in.find("*/", j + 2);
				j = end == std::string_view::npos ? in.size() : end + 2;
			} else {
				break;
			}
		}
		return j;
	};
	while (i < in.size()) {
		char c = in[i];
		if (c == '"') {
			// A string, with its escapes
			size_t j = i + 1;
			while (j < in.size() && in[j] != '"')
				j += in[j] == '\\' ? 2 : 1;
			j = std::min(j + 1, in.size());
			out.append(in.substr(i, j - i));
			i = j;
		} else if (in.compare(i, 2, "//") == 0 || in.compare(i, 2, "/*") == 0) {
			size_t j = skipSpaceAndComments(i);
			out.append(in.substr(i, j - i));
			i = j;
		} else if (c == ',') {
			size_t j = skipSpaceAndComments(i + 1);
			if (j < in.size() && (in[j] == '}' || in[j] == ']')) {
				i++; // dropped
				continue;
			}
			out.push_back(c);
			i++;
		} else {
			out.push_back(c);
			i++;
		}
	}
	return out;
}

bool ParseJsonLenient(std::string_view text, Json::Value& out, std::string& error)
{
	error.clear();
	out = Json::Value();
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
		text.remove_prefix(3);
	std::string cleaned = DropTrailingCommas(text);

	Json::CharReaderBuilder builder;
	builder["allowComments"] = true;
	builder["collectComments"] = false;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	if (cleaned.find_first_not_of(" \t\r\n") == std::string::npos) {
		error = "empty";
		return false;
	}
	bool ok;
	try {
		ok = reader->parse(cleaned.data(), cleaned.data() + cleaned.size(), &out, &error);
	} catch (const std::exception& e) {
		// jsoncpp throws past its nesting limit
		ok = false;
		error = e.what();
	}
	if (!ok) {
		if (error.empty())
			error = "not valid JSON";
		out = Json::Value();
		return false;
	}
	return true;
}

bool ReadJsonFileLenient(const fs::path& path, Json::Value& out, std::string& error)
{
	out = Json::Value();
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		error = "can't open " + Utf8(path);
		return false;
	}
	std::stringstream contents;
	contents << in.rdbuf();
	std::string text = contents.str();
	if (!ParseJsonLenient(text, out, error)) {
		error = Utf8(path) + ": " + error;
		return false;
	}
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
