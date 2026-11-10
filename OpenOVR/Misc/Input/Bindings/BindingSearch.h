#pragma once

// Which binding file a game's controller uses: the user's own first, then the one next to the game, then the
// game's default

#include <filesystem>
#include <optional>
#include <string>

/** In order: <userBindingsDir>/<appKey>/<controllerType>.json, <workingDir>/OpenComposite/<controllerType>.json (as
 * before), <manifestDir>/<defaultBindingUrl>. The first that exists; nothing if none does. */
std::optional<std::filesystem::path> FindBindingFile(const std::string& appKey, const std::string& controllerType,
    const std::filesystem::path& userBindingsDir, const std::filesystem::path& workingDir, const std::filesystem::path& manifestDir,
    const std::string& defaultBindingUrl);

/** The user's binding folder: next to the store, %LOCALAPPDATA%\OpenComposite\bindings (Linux: $XDG_STATE_HOME or
 * ~/.local/state, then OpenComposite/bindings); OC_BINDINGS_DIR instead if it's set (tests). Empty if there's none. */
std::filesystem::path UserBindingsDir();
