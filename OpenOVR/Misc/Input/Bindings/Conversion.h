#pragma once

// A game's binding file for one controller turned into one for another it has none for: each source moved to
// the input that plays the same part on the other controller (the trigger, the grip, the first and second face
// buttons, the menu or system button, the pad or stick), its mode kept, its actions never changed. A source whose
// input the other controller hasn't is dropped with a warning.

#include "BindingFile.h"

#include <optional>
#include <string>

BindingFile ConvertBindingFile(const BindingFile& from, const std::string& toControllerType);

/** One input source path (/user/hand/left/input/x) of fromType's on toType: nothing if toType has no counterpart, or
 * either type is one the table doesn't know */
std::optional<std::string> ConvertSourcePath(const std::string& path, const std::string& fromType, const std::string& toType);
