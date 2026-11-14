#include "Numbers.h"

#include "logging.h"

#include <charconv>
#include <system_error>

namespace {

template <typename T>
std::optional<T> Parse(std::string_view text, size_t* used)
{
	size_t start = 0;
	while (start < text.size() && (text[start] == ' ' || (text[start] >= '\t' && text[start] <= '\r')))
		start++;
	// from_chars takes a '-' but not a '+'
	if (start + 1 < text.size() && text[start] == '+' && text[start + 1] != '-' && text[start + 1] != '+')
		start++;
	T value{};
	const char* first = text.data() + start;
	const char* last = text.data() + text.size();
	std::from_chars_result r = std::from_chars(first, last, value, std::chars_format::general);
	if (r.ec != std::errc())
		return std::nullopt;
	if (used)
		*used = (size_t)(r.ptr - text.data());
	return value;
}

std::string Format(double value, std::chars_format format, int precision)
{
	// A double in %f takes up to 309 digits before the point
	char buffer[512];
	std::to_chars_result r = std::to_chars(buffer, buffer + sizeof(buffer), value, format, precision);
	if (r.ec != std::errc())
		return {};
	return std::string(buffer, r.ptr);
}

} // namespace

std::optional<double> Numbers::ParseDouble(std::string_view text, size_t* used)
{
	return Parse<double>(text, used);
}

std::optional<float> Numbers::ParseFloat(std::string_view text, size_t* used)
{
	return Parse<float>(text, used);
}

float Numbers::ToFloat(std::string_view text, float fallback)
{
	std::optional<float> v = Parse<float>(text, nullptr);
	if (!v) {
		std::string shown(text.substr(0, 64));
		OOVR_LOGF("Not a number (or out of range): '%s', using %g", shown.c_str(), fallback);
		return fallback;
	}
	return *v;
}

float Numbers::ToFloat(std::wstring_view text, float fallback)
{
	// Numbers are ASCII; anything else ends the number
	std::string narrow;
	for (wchar_t c : text)
		narrow.push_back(c > 0 && c < 128 ? (char)c : '\x7f');
	return ToFloat(std::string_view(narrow), fallback);
}

std::string Numbers::FormatGeneral(double value, int precision)
{
	return Format(value, std::chars_format::general, precision);
}

std::string Numbers::FormatFixed(double value, int decimals)
{
	return Format(value, std::chars_format::fixed, decimals);
}
