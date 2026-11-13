#pragma once

// The old input as SteamVR maps it, for inputEvaluator=steamvr: a game reading GetControllerState gets what
// SteamVR's legacy binding file for the controller makes of its raw inputs. Their action set is /actions/legacy; their
// outputs are named <hand>_<button>_<press|touch|value|value1|value2> (right_axis0_value, left_system_press...), and
// land on VRControllerState_t's ulButtonPressed, ulButtonTouched and rAxis[0..4].

#include "BindingFile.h"
#include "Modes.h"

#include "generated/interfaces/vrtypes.h"

#include <filesystem>
#include <optional>
#include <string>

/** The action set legacy binding files bind */
inline const char* const kLegacyActionSet = "/actions/legacy";

/** Where in SteamVR's folder the legacy binding file of a controller type is (the generic one for a type it has none
 * for) */
std::filesystem::path LegacyBindingsRelativePath(const std::string& controllerType);

/** The legacy binding file for a controller type: SteamVR's own from steamvrDir if it's there, else OpenComposite's
 * built-in one (written to map as SteamVR's do). Never empty: a type neither knows gets the generic one. */
BindingFile LoadLegacyBindingsFrom(const std::string& controllerType, const std::optional<std::filesystem::path>& steamvrDir);

/** As LoadLegacyBindingsFrom, with SteamVR's install (FindSteamVRInstall); read once per type and kept for good (the
 * pointer stays valid) */
const BindingFile* LoadLegacyBindings(const std::string& controllerType);

/** OpenComposite's own legacy binding file for a controller type (the generic one for a type it has none for) */
BindingFile BuiltInLegacyBindings(const std::string& controllerType);

/** One hand's legacy outputs (hand: "left" or "right") written into a controller state: presses, touches and axes */
void ApplyLegacyOutputs(const ActionOutputs& out, const std::string& hand, vr::VRControllerState_t& state);

/** The buttons (as Prop_SupportedButtons_Uint64) a legacy binding file gives one hand */
uint64_t LegacySupportedButtons(const BindingFile& file, const std::string& hand);

/** An axis's type (as Prop_Axis0Type_Int32...) from the mode of the first source writing it in a legacy binding file */
vr::EVRControllerAxisType LegacyAxisType(const BindingFile& file, const std::string& hand, int axis);
