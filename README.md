# OpenComposite (fzeruhn's fork)

[![Build](https://img.shields.io/github/actions/workflow/status/fzeruhn/OpenComposite/build.yml?branch=openxr&style=for-the-badge&logo=github&label=build)](https://github.com/fzeruhn/OpenComposite/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/fzeruhn/OpenComposite?include_prereleases&style=for-the-badge&label=release)](https://github.com/fzeruhn/OpenComposite/releases)

Play SteamVR (OpenVR) games on any OpenXR runtime, without SteamVR.

This is a fork of [OpenComposite](https://gitlab.com/znixian/OpenOVR) by ZNix and its contributors. Upstream hasn't
merged anything since July 2025, and its open merge requests, some of them needed by newer games such as Half-Life:
Alyx, have waited without review. This fork takes them, fixes what review found in them, adds fixes of its own, and
publishes Windows and Linux builds.

**Bugs and questions about this fork go to [its issues](https://github.com/fzeruhn/OpenComposite/issues)**, not to
upstream's GitLab or Discord: they can't help with changes they haven't made.

## What this fork has that upstream doesn't

Upstream merge requests are linked as !*n* with their authors. Everything else was written for this fork.

**Newer games**
- The interfaces of OpenVR SDK 2.12 and 2.15: `IVRSystem_023` and `_026`, `IVRCompositor_029`, `IVRInput_011`,
  `IVROverlay_028`, `IVRApplications_008` and `IVRNotifications_002`. Games built with those SDKs (Half-Life: Alyx,
  No Man's Sky, Batman: Arkham VR, Portal 2: Community Edition and others) no longer stop at "Missing interface".
  ([!189](https://gitlab.com/znixian/OpenOVR/-/merge_requests/189), Faye Salwin)
- Half-Life: Alyx: `IVRIPCResourceManagerClient` is answered as not found instead of stopping the game, and
  `VRControlPanel()` is exported. ([!190](https://gitlab.com/znixian/OpenOVR/-/merge_requests/190), arealjim)
- Action manifests with eye-tracking actions are read, skipping those actions, instead of aborting. So are long action
  names a game uses before it has asked for their handle.

**Input**
- Dpad bindings work in every action set, not only the first one synced (No Man's Sky's snap turning), and a
  double-click binding counts one press. ([!186](https://gitlab.com/znixian/OpenOVR/-/merge_requests/186), Faye Salwin)
- Valve Index trackpad dpad clicks: several click bindings on one trackpad no longer abort, and their thresholds no
  longer overwrite the action's own.
- Index controller bindings on controllers without a force-sensing trackpad (Vive, WMR) no longer abort.
- Index grip actions are cleared when a game reloads its action manifest.
- Generic trackers work on runtimes without the full `XR_MNDX_xdev_space` extension, which is now checked before use.
  ([!184](https://gitlab.com/znixian/OpenOVR/-/merge_requests/184), KaiErikNiermann)

**Frame timing**
- `GetFrameTiming` reports measured frame times and dropped frames instead of constants, so games that adjust their
  resolution to the load (Half-Life: Alyx's auto-fidelity and others) can.
  ([!191](https://gitlab.com/znixian/OpenOVR/-/merge_requests/191), arealjim)
- F1 23 no longer crashes in `GetFrameTiming`; each game's frame timing struct is filled within its own size, including
  Proton's padded one. ([!114](https://gitlab.com/znixian/OpenOVR/-/merge_requests/114), Manuel Kraus)
- Older `IVRCompositor` versions (`_009` to `_016`) get a struct of their own size; Proton 9 and later overran
  `_016`'s by 4 bytes.

**Vulkan and Direct3D 11**
- Vulkan starts on devices without external memory, and accepts one GPU seen through several Vulkan instances by
  passing the runtime's own device. ([!187](https://gitlab.com/znixian/OpenOVR/-/merge_requests/187), Faye Salwin)
- Vulkan games whose queue isn't in family 0 get a command pool of their queue's family.
- `invertUsingShaders` handles typeless and array textures, and falls back to a copy for textures the shader can't
  read. ([!96](https://gitlab.com/znixian/OpenOVR/-/merge_requests/96), Zak Ahmed)
- The D3D11 copy uses the right subresource, and resolves multisampled (MSAA) textures.

**Overlays**
- Overlays get the right position, rotation, size and sort order, follow the device they are attached to (a wrist
  menu), hide when that device isn't tracked, and laser pointers hit them where they are drawn.
  ([!150](https://gitlab.com/znixian/OpenOVR/-/merge_requests/150), mittorn)
- Curved overlays, through `XR_KHR_composition_layer_cylinder`, drawn flat where the runtime lacks it.
  ([!130](https://gitlab.com/znixian/OpenOVR/-/merge_requests/130), galister)

**Stability and building**
- The log opens before anything writes to it (upstream issue #479), and a log that can't be opened no longer crashes
  the game. ([!185](https://gitlab.com/znixian/OpenOVR/-/merge_requests/185), Patrick Nicolas)
- Null pointers, zero frame counts and buffer sizes are checked where games can pass them.
- Builds with current MSVC (`<chrono>`: [!188](https://gitlab.com/znixian/OpenOVR/-/merge_requests/188), Faye
  Salwin), with clang, with optimised gcc and with Python 3.12 and later. Linux arm64, Android and macOS builds go to
  their own output folders. ([!179](https://gitlab.com/znixian/OpenOVR/-/merge_requests/179), Kitlith)

**Releases**
- [Windows and Linux builds](https://github.com/fzeruhn/OpenComposite/releases), 64- and 32-bit, built by GitHub
  Actions from the tagged commit, with checksums, the licences and the full source. Upstream's download links and
  launcher fetch upstream's builds.

Every change was reviewed, and all four builds are made with warnings as errors on every change. Not every change has
been tried yet in the game it is for. If something works upstream and not here, please
[open an issue](https://github.com/fzeruhn/OpenComposite/issues/new).

Not here yet: overlay apps (upstream !101), controller models and the virtual keyboard.

## What OpenComposite is

OpenComposite is an implementation of OpenVR, SteamVR's API, that forwards a game's calls straight to the OpenXR
runtime: Meta's, Monado, Windows Mixed Reality, Pimax's, Varjo's and others. Think of it as a backwards
[ReVive](https://github.com/LibreVR/Revive). Games run on your headset as though they were native OpenXR games,
without SteamVR in between. Reasons to use it:

* Some machines have trouble running SteamVR.
* SteamVR can take a while to start, and is several gigabytes.
* OpenXR runtimes can be less prone to the stutter some people get in SteamVR.

It used to be called OpenOVR ("OpenVR for OculusVR") and was renamed to avoid confusion with OpenVR.

Two community resources written for upstream mostly apply here too: mbucchia's
[FAQ](https://mbucchia.github.io/OpenXR-Toolkit/opencomposite.html) and the
[game compatibility spreadsheet](https://docs.google.com/spreadsheets/d/1s2SSuRt0oHm91RUZB-R-ef5BrfOtvP_jwDwb6FuMF9Q/edit#gid=2068512515).
A game listed there as broken by a missing interface may work with this fork.

## Installing

Download from the [releases page](https://github.com/fzeruhn/OpenComposite/releases): `OpenComposite-windows.zip` or
`OpenComposite-linux.tar.gz`. Each holds an `OpenComposite` folder with the 64- and 32-bit libraries in `bin`. Your
headset's OpenXR runtime must be the active one.

An installer for Windows and Linux is being worked on; this section will describe it. Until then, by hand: unpack the
folder somewhere it can stay, and put it first in `"runtime"` of `openvrpaths.vrpath`
(`%LOCALAPPDATA%\openvr\openvrpaths.vrpath` on Windows, `~/.config/openvr/openvrpaths.vrpath` on Linux), keeping the
other entries after it. To switch back, put SteamVR's folder first again.

### Linux-specific info

As most VR games are Windows games run through Proton, you'll usually install system-wide, but an environment variable
lets you choose per game. Unpack the tarball, which gives an `OpenComposite` folder, and then:

1. Get the runtime in place, by one of these:
	- Set `VR_OVERRIDE` to the `OpenComposite` folder, in the game's Steam launch options:
	  `VR_OVERRIDE=/path/to/OpenComposite %command%`
	- Edit `~/.config/openvr/openvrpaths.vrpath`, and put the `OpenComposite` folder first in `"runtime"`. Steam likes
	  to rewrite this file, so you may need to make it read-only.
	- For the rare native Linux game, copy `bin/linux64/vrclient.so` over the game's `libopenvr_api.so`.
2. Games in the Steam Runtime (most of them) need a few paths made visible inside it:
	- Files in your home directory are there as they are.
	- Paths under `/usr` are under `/run/host/usr`. For example, a system-wide Monado's runtime JSON is
	  `/run/host/usr/share/openxr/1/openxr_monado.json`.
	- Other paths can be added with the `PRESSURE_VESSEL_FILESYSTEMS_RW` environment variable.

For example, with OpenComposite in your home directory, `VR_OVERRIDE`, and Monado installed system-wide, the launch
options might be:

```
VR_OVERRIDE=~/OpenComposite XR_RUNTIME_JSON=/run/host/usr/share/openxr/1/openxr_monado.json PRESSURE_VESSEL_FILESYSTEMS_RW=$XDG_RUNTIME_DIR/monado_comp_ipc %command%
```

`$XDG_RUNTIME_DIR/monado_comp_ipc` is Monado's socket: the game needs to see it to find Monado running. More on
`PRESSURE_VESSEL_FILESYSTEMS_RW` and the Steam Runtime's other variables is in
[its documentation](https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/master/pressure-vessel/wrap.1.md).

The Linux builds need glibc 2.38 or later (Ubuntu 24.04, Debian 13, Fedora 39 or newer).

## Configuration file

On startup, OpenComposite searches for a file named `opencomposite.ini`, first next to the `openvr_api.dll` file, and if it can't find
that, then it looks in the working directory (usually in the same folder as the EXE). If no file is found, that's fine and all
the default values are used.

This is its configuration file, where you can specify a number of options. These are listed in `name=value` format, and you
can define a comment by starting a line with `;` or a `#`. Note the option names **are** case sensitive. An option OpenComposite
doesn't know, or a value it can't read, is logged as a warning (in `opencomposite.log`) and that option keeps its default; the game
starts either way. Any whitespace between the name, equals sign, and value is ignored, as is whitespace on the end of the line.
Ensure your lines do not begin with whitespace however.

The available options are:

* `renderCustomHands` - boolean, default `enabled`. 
	* Should OpenComposite render custom hands. Disable this if you *really* dislike the hand models, and some games (like Skyrim) come with backup models that will be used instead.
* `handColour` - colour, default `#4c4c4c`. 
	* The colour of the hands OpenComposite draws, as they currently lack proper textures.
* `supersampleRatio` - float, default `1.0`. 
	* The supersample ratio in use - this is similar to what you would enter into SteamVR, a value of `145%` in SteamVR is a value of `1.45` here. A value of `80%` in SteamVR is `0.8` here, and so on. Higher numbers improve graphics, at a major performance cost.
* `haptics` - boolean, default `enabled`. 
	* Should haptic feedback to the controllers be enabled.
* `admitUnknownProps` - boolean, default `disabled`. 
	* If asked for a tracked device property it does not understand, should OpenComposite ignore it.
* `logGetTrackedProperty` - boolean, default `disabled`. 
	* Print logging information when the app requests information about tracked devices, such as the HMD or the Touch controllers. On some games, this causes a log entry to be generated every frame, which isn't great for performance and clutters up the log. This is potentially useful for troubleshooting, and was enabled by default before the config option existed. In general, unless you've been told to enable this (or you know what you're doing while troubleshooting) you don't need to enable this.
* `stopOnSoftAbort` - boolean, default `disabled`.
    * Ends the game at the first call OpenComposite can't answer properly, as it used to. By default such a call gets an error or an empty answer and the game goes on; each one is logged the first time, and when the game shuts OpenVR down the log ends with a compatibility summary (`OpenComposite compatibility summary: <n> unsupported calls in <m> places`, then each place with how often it was hit). That list is what's worth reporting when a game misbehaves. Only enable this to find where a game first goes wrong.
* `dx10Mode` - boolean, default `disabled`.
    * Use DX10 instead of DX11 for games submitting DirectX textures.
* `enableHiddenMeshFix` - boolean, default `enabled`. 
	* Alter the coordinates of the hidden area mesh mask to fit within the view's projection. The hidden area mesh mask defines a region to which the game will not draw and is used to mask off areas of the display that you typically cannot see in the headset. If you see odd black regions around the view then try disabling this fix.
* `invertUsingShaders` - boolean, default `disabled`. 
	* Invert the image for display using shaders rather than relying on the XR runtime and inverted FOV values. Some games render the image inverted and rely on the runtime to display correctly. In OpenXR some runtimes don't do the inversion correctly. If so enable this option for OpenComposite to do the inversion when copying the image using shaders. This might have a minor cost in performance.
* `initUsingVulkan` - boolean, default `true`. 
	* If available the temporary graphics adapter at start up will use Vulkan by default. This may be incompatible with some games. This will mostly affect Oculus headsets. 
* `hiddenMeshVerticalScale` - float, default `1.0`. 
	* The scaling factor used for the hidden area mesh if supported by the application. The hidden area mesh is a region that the game doesn't render to. If you set this lower e.g. `0.8` then less will be drawn at the very top and very bottom of the image improving performance. Suggested range is `0.5` to `1.0`.
* `logAllOpenVRCalls` - boolean, default `false`
	* Log every OpenVR call a game makes. Similar to `logGetTrackedProperty`, this clutters logs and should not be enabled unless necessary.

The possible types are as follows:

* boolean - one of `on`, `true`, `enabled` to enable the setting, or `off`, `false` or `disabled` to do the opposite. These
are not case-sensitive, so `Enabled` or `tRUe` work fine.
* colour - same as the format used in HTML/CSS, it's a `#` followed by six hex characters. Mozilla has a
[tool](https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_Colors/Color_picker_tool) to help with this. Again, it's case-insensitive.
* float - a floating point (decimal) number, eg `123.456`.

Example files:

- Running SkyrimVR with yellow hands, and haptic feedback disabled:

```
handColour=#ffff00
haptics = off
```

## Reporting a bug

If a game crashes, shows a missing interface, or behaves differently than under SteamVR, please
[open an issue here](https://github.com/fzeruhn/OpenComposite/issues/new). Say:

- the game, your headset and OpenXR runtime, and which release of this fork (or commit) you use;
- what happened and when: at start, after loading a level, on pressing a button;
- whether it also happens with upstream's build, if you know.

And attach `opencomposite.log`, which has most of what's needed to find the cause. It is in:

- Windows: `%LOCALAPPDATA%\OpenComposite\logs` (paste that into Explorer's address bar)
- Linux: `$XDG_STATE_HOME/OpenComposite/logs`, or `~/.local/state/OpenComposite/logs` if `XDG_STATE_HOME` isn't set

If neither exists, look for `opencomposite.log` next to the game's EXE.

## Building

The source is GPL-3.0-or-later. Clone with its submodules:

```bash
git clone --recursive https://github.com/fzeruhn/OpenComposite.git
```

It's a standard CMake project, so IDEs such as CLion and Visual Studio can build it too. The scripts below build it
the way the releases are built: Release, warnings as errors (`-DERROR_ON_WARNING=ON`). Pull requests are built the same
way on all four targets, so please make sure yours builds without warnings.

**Windows:** Visual Studio 2022 or newer, or its Build Tools, with the C++ x64/x86 tools and "C++ ATL"; Python 3; and
the Vulkan headers and loader, for example from vcpkg (`vcpkg install vulkan-headers vulkan-loader --triplet
x64-windows`). Then, in PowerShell 7:

```powershell
scripts\ci\build-windows.ps1 -Arch x64 -Vulkan C:\vcpkg\installed\x64-windows
```

This makes `build\windows-x64\bin\vrclient_x64.dll` (`-Arch x86` makes `vrclient.dll`). The script copies the Vulkan
files to `libs\vulkan`, where `CMakeLists.txt` looks for them; to build without Vulkan, remove `SUPPORT_VK` from the
preprocessor definitions.

**Linux:** the packages in `scripts/ci/ubuntu.dockerfile` (gcc with multilib, CMake, the autotools, Python 3, and the
GL and Vulkan development files), then:

```bash
scripts/ci/build-linux.sh x64
```

This makes `build/linux-x64/bin/linux64/vrclient.so` (`x86` makes `bin/vrclient.so`). A plain `cmake -B build && cmake
--build build` works too, and the build folder can then be used directly as `VR_OVERRIDE`.

`RuntimeSwitcher/` is upstream's Windows launcher. It downloads upstream's builds, so it isn't part of this fork's
releases.

### How the code is generated

OpenComposite relies heavily on scripts that generate code; without them the repository would have roughly three to four
times as much. There are two:

1. One splits OpenVR's headers into one header per version of each interface, so the project can use many versions of
   the same interface.
2. One generates the interface stubs. Each interface has one implementation (`BaseSystem`, `BaseCompositor` and so
   on), and generated stub classes implement a binary-compatible version of each version of it (`GVRSystem_017`,
   `GVRSystem_015`, `GVRCompositor_022`), with C-like `FnTable`s for them (for other languages, notably C#).

Both run as part of the build. Adding or removing an OpenVR header makes CMake rebuild the project; otherwise only a
couple of files are rebuilt. The `doc/` folder says more about why the scripts exist and how they work.

### Adding a new OpenVR SDK

`tests/conformance/interfaces.txt` lists every interface version Valve has shipped, and `scripts/openvr_inventory.py`
keeps it up to date. When Valve tags a new SDK:

1. Clone Valve's repository without its files: `git clone --filter=blob:none --no-checkout https://github.com/ValveSoftware/openvr.git <dir>`.
2. Refresh the list: `python scripts/openvr_inventory.py --valve-repo <dir> --write tests/conformance/interfaces.txt`.
3. Add the new SDK's `headers/openvr.h` to `OpenVRHeaders/` as `openvr-<version>.h`, byte for byte.
4. Add a `GEN_INTERFACE` line in the interface's `OpenOVR/Reimpl/CVR*.cpp` file for each new version, and write the
   `Base*` methods it needs.
5. Check: `python scripts/openvr_inventory.py --check` lists every version that isn't offered yet. A version answered
   "not found" on purpose goes in `tests/conformance/not_implemented.txt` with its reason.

## Licence

OpenComposite itself is Free Software under the GPLv3:

```
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
```

[GPL Full Text](https://www.gnu.org/licenses/gpl-3.0-standalone.html). This also can be found in
the `LICENSE.txt` file supplied with the source code.

This fork is a modified version of [upstream OpenComposite](https://gitlab.com/znixian/OpenOVR): its changes since
upstream's commit `cff07db` are listed above and in this repository's history.

### OpenXR SDK

The OpenXR SDK is included as a git submodule. It's under the Apache 2 licence, the full text of
which can be found under `libs/openxr-sdk/LICENSE`.

### OpenVR

This program (and its source code) includes several versions of the
[OpenVR](https://github.com/ValveSoftware/openvr) headers. These are under the
[MIT Licence](https://github.com/ValveSoftware/openvr/blob/master/LICENSE). This also can be
found in the `LICENSE_OPENVR.txt` file supplied with the source code.

```
Copyright (c) 2015, Valve Corporation
All rights reserved.
```

### INIH

This program uses the [inih](https://github.com/benhoyt/inih) INI parsing library by Ben Hoyt. This is available under
the MIT licence, a copy of which can be found in the `LICENSE_INIH.txt` file of this repository.

### Microblink Build

This program uses a part of Psiha's and Microblink's [build](https://github.com/microblink/build) CMake utilities
library. This is available under the two-clause BSD licence, a copy of which can be found in the `LICENCE_BUILD_UTIL.txt`
file of this repository.

### DebugBreak

This program uses Scott Tsai's [DebugBreak](https://github.com/scottt/debugbreak) library when built on Linux. Its full
text can be found in `LICENCE_DEBUG_BREAK.txt`.
