#pragma once
// Numbers as text, read and written the same in every locale: a game may set LC_NUMERIC to one with a decimal comma,
// which strtof, std::stof and printf follow (and "0.55" would read as 0).

#include <optional>
#include <string>
#include <string_view>

namespace Numbers {

// The number at the start of text, after any spaces and a '+', as strtod reads it: nothing if there's none there
// (or it's out of range). *used is how many characters it took, the spaces included.
std::optional<double> ParseDouble(std::string_view text, size_t* used = nullptr);
std::optional<float> ParseFloat(std::string_view text, size_t* used = nullptr);

// The number at the start of text, or fallback (logged) if there's none there or it's out of range
float ToFloat(std::string_view text, float fallback);
float ToFloat(std::wstring_view text, float fallback);

// As printf's %.<precision>g and %.<decimals>f
std::string FormatGeneral(double value, int precision);
std::string FormatFixed(double value, int decimals);

} // namespace Numbers
