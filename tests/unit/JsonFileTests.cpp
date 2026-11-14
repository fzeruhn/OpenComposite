// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/JsonFile.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct TempFile {
	fs::path path = fs::temp_directory_path() / ("oc_jsonfile_test_" + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id())) + ".json");
	TempFile() { fs::remove(path); }
	~TempFile() { fs::remove(path); }
	void Write(const std::string& text) const { std::ofstream(path, std::ios::binary) << text; }
};
} // namespace

TEST_CASE("JsonFile_missing_file_reads_as_an_empty_object")
{
	TempFile f;
	Json::Value v(42);
	CHECK_FALSE(oc_read_json_file(f.path, v));
	CHECK(v.isObject());
	CHECK(v.empty());
}

TEST_CASE("JsonFile_half_written_file_reads_as_an_empty_object")
{
	// What a second program sees while the first is half-way through writing applist.json
	TempFile f;
	f.Write("{\"C:\\\\Games\\\\a.exe\":{\"timestamp\":17");
	Json::Value v;
	CHECK_FALSE(oc_read_json_file(f.path, v));
	CHECK(v.isObject());
	CHECK(v.empty());
}

TEST_CASE("JsonFile_empty_file_and_non_object_read_as_an_empty_object")
{
	TempFile f;
	Json::Value v;
	f.Write("");
	CHECK_FALSE(oc_read_json_file(f.path, v));
	CHECK(v.isObject());
	f.Write("[1, 2]");
	CHECK_FALSE(oc_read_json_file(f.path, v));
	CHECK(v.isObject());
}

TEST_CASE("JsonFile_round_trip")
{
	TempFile f;
	Json::Value v(Json::objectValue);
	v["C:\\Games\\a.exe"]["timestamp"] = Json::UInt64(1762560000000ULL);
	CHECK(oc_write_json_file(f.path, v));
	Json::Value back;
	CHECK(oc_read_json_file(f.path, back));
	// jsoncpp reads the number back as a signed one, so compare the value
	CHECK(back["C:\\Games\\a.exe"]["timestamp"].asUInt64() == 1762560000000ULL);
	CHECK(back.size() == 1);
}

TEST_CASE("JsonFile_concurrent_writers_never_leave_a_broken_file")
{
	TempFile f;
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; t++) {
		threads.emplace_back([&, t] {
			for (int i = 0; i < 50; i++) {
				Json::Value v(Json::objectValue);
				v["writer"] = t;
				v["round"] = i;
				v["padding"] = std::string(4096, 'x');
				oc_write_json_file(f.path, v);
				Json::Value back;
				oc_read_json_file(f.path, back);
			}
		});
	}
	for (auto& th : threads)
		th.join();
	Json::Value last;
	CHECK(oc_read_json_file(f.path, last));
	CHECK(last.isMember("writer"));
}

TEST_CASE("JsonFile_write_to_a_missing_folder_fails_quietly")
{
	Json::Value v(Json::objectValue);
	CHECK_FALSE(oc_write_json_file(fs::temp_directory_path() / "oc_no_such_folder" / "x" / "a.json", v));
}

TEST_CASE("Lenient_json_reads_bom_comments_trailing_commas")
{
	Json::Value v;
	std::string error;
	CHECK(ParseJsonLenient("\xEF\xBB\xBF{ // c\r\n \"a\": [1,2,], /* block, with ] and } */ \"b\": \"x,]\", }", v, error));
	CHECK(error.empty());
	CHECK(v["a"].size() == 2);
	// A comma inside a string isn't trailing
	CHECK(v["b"].asString() == "x,]");
}

TEST_CASE("Lenient_json_reports_garbage")
{
	Json::Value v;
	std::string error;
	CHECK_FALSE(ParseJsonLenient("{ \"a\": [1, 2 ", v, error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(ParseJsonLenient("", v, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("Lenient_json_file_reads_the_manifest_fixture")
{
	Json::Value v;
	std::string error;
	CHECK(ReadJsonFileLenient(fs::path(OC_TEST_DATA_DIR) / "manifest_bom_comments.json", v, error));
	CHECK(v["actions"].size() == 4);
	CHECK_FALSE(ReadJsonFileLenient(fs::path(OC_TEST_DATA_DIR) / "no_such_file.json", v, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("Json_past_the_nesting_limit_is_unparsable_not_fatal")
{
	// jsoncpp throws past its nesting limit (1000)
	std::string deep = "{\"a\":" + std::string(2000, '[') + std::string(2000, ']') + "}";
	Json::Value v;
	std::string error;
	CHECK_FALSE(ParseJsonLenient(deep, v, error));
	CHECK_FALSE(error.empty());
	TempFile f;
	f.Write(deep);
	CHECK_FALSE(oc_read_json_file(f.path, v));
	CHECK(v.isObject());
}
