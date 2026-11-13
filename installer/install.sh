#!/bin/sh
# OpenComposite's Linux installer, in the OpenComposite folder of OpenComposite-linux.tar.gz and in the installed copy.
# It installs for the current user only: no root. ./install.sh --help says what it does.
set -eu

REPOSITORY=fzeruhn/OpenComposite
FLATPAK_STEAM=com.valvesoftware.Steam
ARCHIVE=OpenComposite-linux.tar.gz

usage() {
	cat <<'EOF'
Usage: install.sh [command] [--flatpak | --native]

Commands (one; with none, install):
  (none)            Copy OpenComposite to ~/.local/share/OpenComposite and make OpenVR games use it: it goes first in
                    the "runtime" list of ~/.config/openvr/openvrpaths.vrpath, other entries kept after it. The file
                    is backed up to openvrpaths.vrpath.bak first. Files you put in bin/ (opencomposite.ini,
                    apps-config.json) are kept.
  --no-switch       Install or update the copy, but leave openvrpaths.vrpath as it is.
  --opencomposite   Make OpenVR games use the installed copy.
  --steamvr         Make OpenVR games use SteamVR again: the copy is taken out of the list and SteamVR's folder put
                    first. Starting SteamVR from Steam does that too.
  --uninstall       --steamvr, then delete the installed copy.
  --status          Say what OpenVR games use, what is installed, and the OpenXR runtime.
  --update [--pre]  Download the newest release from github.com/fzeruhn/OpenComposite (--pre: pre-releases too),
                    check its SHA-256, and install it if it is newer. The switch is left as it is.
  --launch-options  Print the Steam launch options games in the Steam Runtime (most Proton games) need to reach your
                    OpenXR runtime.
  -h, --help        This.

Steam installed as a Flatpak (com.valvesoftware.Steam) has its own openvrpaths.vrpath and can't see
~/.local/share, so for it the copy goes in ~/.var/app/com.valvesoftware.Steam/.local/share/OpenComposite. That is
chosen by itself when only Flatpak Steam is found; --flatpak or --native chooses.

Needs python3 (to edit the JSON file); --update also needs curl or wget, and sha256sum.
EOF
}

say() { printf '%s\n' "$*"; }
die() {
	printf 'install.sh: %s\n' "$*" >&2
	exit 1
}

command=install
target=auto
pre=0
while [ $# -gt 0 ]; do
	case $1 in
	--no-switch) command=copy ;;
	--opencomposite) command=use ;;
	--steamvr) command=steamvr ;;
	--uninstall) command=uninstall ;;
	--status) command=status ;;
	--update) command=update ;;
	--pre) pre=1 ;;
	--launch-options) command=launch ;;
	--flatpak) target=flatpak ;;
	--native) target=native ;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		usage >&2
		exit 2
		;;
	esac
	shift
done
if [ $pre = 1 ] && [ $command != update ]; then
	die "--pre goes with --update."
fi
[ "$(id -u)" != 0 ] || die "this installs for one user: run it as your own user, not as root (no sudo)."

home=${HOME:?"HOME is not set"}
here=$(cd "$(dirname "$0")" && pwd -P)
flatpak_home=$home/.var/app/$FLATPAK_STEAM
if [ $target = auto ]; then
	if [ -d "$flatpak_home" ] && [ ! -d "$home/.steam/steam" ] && [ ! -d "${XDG_DATA_HOME:-$home/.local/share}/Steam" ]; then
		target=flatpak
	else
		target=native
	fi
fi
if [ $target = flatpak ]; then
	# The Flatpak keeps its home in ~/.var/app/<id>, which it also sees at that path.
	paths_file=$flatpak_home/.config/openvr/openvrpaths.vrpath
	copy=$flatpak_home/.local/share/OpenComposite
else
	paths_file=${XDG_CONFIG_HOME:-$home/.config}/openvr/openvrpaths.vrpath
	copy=${XDG_DATA_HOME:-$home/.local/share}/OpenComposite
fi
target_flag=--$target

# Both kinds of Steam: say how to reach the other one.
other_steam() {
	if [ $target = native ] && [ -d "$flatpak_home" ]; then
		say "Flatpak Steam is installed too; for it, run this again with --flatpak."
	fi
}

command -v python3 >/dev/null 2>&1 || die "python3 is needed (to edit $paths_file). Install it with your package manager."

# The JSON work: openvrpaths.vrpath's "runtime" list, and GitHub's release list. Every key of the paths file other than
# "runtime" is written back as it was read, in its order.
PY=$(
	cat <<'EOF'
import json, os, re, shutil, sys

def steamvr(path):
    return os.path.basename(path.rstrip("/")) == "SteamVR"

def same(a, b):
    return a.rstrip("/") == b.rstrip("/")

def load(file):
    if not os.path.exists(file):
        return None
    try:
        with open(file, encoding="utf-8") as f:
            data = json.load(f)
    except OSError as e:
        sys.exit(f"install.sh: can't read {file} ({e.strerror}).")
    except ValueError as e:
        sys.exit(f"install.sh: {file} isn't valid JSON ({e}); fix or delete it, then run this again.")
    runtime = data.get("runtime") if isinstance(data, dict) else None
    if not isinstance(data, dict) or not (runtime is None or isinstance(runtime, list) and all(isinstance(r, str) for r in runtime)):
        sys.exit(f"install.sh: {file} isn't an OpenVR paths file (\"runtime\" isn't a list of folders).")
    return data

# Written next to the real file (a symlink stays a symlink), with its permissions. A file that can't be written, as when
# made read-only to keep SteamVR from changing it, is refused and left as it is.
def save(file, data, old):
    file = os.path.realpath(file)
    folder = os.path.dirname(file)
    temp = file + ".install-tmp"
    try:
        os.makedirs(folder, exist_ok=True)
        if (old is not None and not os.access(file, os.W_OK)) or not os.access(folder, os.W_OK):
            raise PermissionError(None, None, file)
        if old is not None:
            shutil.copyfile(file, file + ".bak")
        with open(temp, "w", encoding="utf-8") as f:
            json.dump(data, f, indent="\t", ensure_ascii=False)
            f.write("\n")
        if old is not None:
            shutil.copymode(file, temp)
        os.replace(temp, file)
    except PermissionError as e:
        if os.path.exists(temp):
            os.remove(temp)
        name = e.filename or file
        sys.exit(f"install.sh: {name} can't be written (it or its folder is read-only), so {'it' if name == file else file} was left as it is.")

def version_key(v):
    m = re.fullmatch(r"(\d+(?:\.\d+)*)(?:-([0-9A-Za-z.-]+))?", v or "")
    if not m:
        return None
    numbers = [int(x) for x in m.group(1).split(".")]
    numbers += [0] * (8 - len(numbers))
    # Pre-release parts as semver orders them: numbers by value and before words, words by character code, and with
    # the shared parts equal, fewer parts first (rc.2 < rc.10 < rc.10.1; a release after its pre-releases).
    pre = [(0, int(x), "") if x.isdigit() else (1, 0, x) for x in (m.group(2) or "").split(".") if x]
    return (numbers, m.group(2) is None, pre)

def main(command, *args):
    if command == "status":
        file, ours = args
        data = load(file)
        runtimes = (data or {}).get("runtime") or []
        first = runtimes[0] if runtimes else ""
        kind = "none" if not first else "opencomposite" if same(first, ours) else "steamvr" if steamvr(first) else "other"
        listed = any(same(r, ours) for r in runtimes)
        # "-" for no first runtime: the shell's read would merge an empty field with its tab neighbours
        print(f"{kind}\t{first or '-'}\t{'listed' if listed else 'unlisted'}\t{'exists' if data is not None else 'missing'}")
    elif command in ("use", "steamvr", "remove"):
        file, ours = args
        data = load(file)
        if data is None:
            if command != "use":
                return
            data = {"jsonid": "vrpathreg", "runtime": [], "version": 1}
            old = None
        else:
            old = data.get("runtime") or []
        runtimes = [r for r in (data.get("runtime") or []) if not same(r, ours)]
        if command == "use":
            runtimes.insert(0, ours)
        elif command == "steamvr" or (old and same(old[0], ours)):
            index = next((i for i, r in enumerate(runtimes) if steamvr(r)), None)
            if index:
                runtimes.insert(0, runtimes.pop(index))
        if old is not None and runtimes == old:
            return
        data["runtime"] = runtimes
        save(file, data, old)
    elif command == "source-version":
        (file,) = args
        try:
            with open(file, encoding="utf-8") as f:
                m = re.fullmatch(r"OpenComposite (\S+), commit [0-9a-f]+", f.readline().strip())
        except OSError:
            m = None
        print(m.group(1) if m and version_key(m.group(1)) else "")
    elif command == "newest":
        # GitHub's release list on stdin: the newest release newer than the installed version (empty: none) with the
        # archive and its SHA-256, as "version<TAB>url<TAB>sha256".
        installed, pre, asset_name = args
        best = None
        for release in json.load(sys.stdin):
            if release.get("draft") or (release.get("prerelease") and pre != "1"):
                continue
            tag = release.get("tag_name") or ""
            version = tag[1:] if tag[:1] in ("v", "V") else tag
            key = version_key(version)
            if key is None or (installed and version_key(installed) and key <= version_key(installed)):
                continue
            for asset in release.get("assets") or []:
                digest = asset.get("digest") or ""
                if asset.get("name") == asset_name and re.fullmatch(r"sha256:[0-9a-fA-F]{64}", digest):
                    if best is None or key > best[0]:
                        best = (key, version, asset["browser_download_url"], digest[7:].lower())
        if best:
            print("\t".join(best[1:]))
    elif command == "runtime-library":
        # The library_path of an OpenXR runtime manifest.
        (file,) = args
        try:
            with open(file, encoding="utf-8") as f:
                print(json.load(f)["runtime"]["library_path"])
        except (OSError, ValueError, KeyError, TypeError):
            print("")

main(*sys.argv[1:])
EOF
)
py() { python3 -c "$PY" "$@"; }

# What OpenVR games use now, tab-separated: kind (opencomposite, the copy; steamvr; other; none), the first entry or
# '-', whether the copy is listed (listed/unlisted), and whether the paths file exists (exists/missing).
current() {
	py status "$paths_file" "$copy"
}

installed_version() {
	py source-version "$copy/SOURCE.txt"
}

# Copies this folder's OpenComposite to $copy. Each file goes to a temporary name first and is then renamed over the
# old one, so a game that has the old library loaded keeps running.
copy_files() {
	if [ ! -f "$here/bin/linux64/vrclient.so" ] || [ ! -f "$here/bin/vrclient.so" ]; then
		die "$here has no bin/linux64/vrclient.so and bin/vrclient.so: run install.sh from the unpacked $ARCHIVE."
	fi
	if [ "$here" = "$(cd "$copy" 2>/dev/null && pwd -P)" ]; then
		return # Run from the installed copy.
	fi
	mkdir -p "$copy/bin/linux64"
	for file in bin/linux64/vrclient.so bin/vrclient.so bin/version.txt SOURCE.txt install.sh; do
		put "$here/$file" "$copy/$file"
	done
	for file in "$here"/LICENSE*.txt "$here"/LICENCE*.txt "$here"/NOTES*.txt; do
		if [ -f "$file" ]; then
			put "$file" "$copy/${file##*/}"
		fi
	done
	say "Installed OpenComposite $(installed_version) in $copy"
}

put() {
	cp "$1" "$2.install-tmp"
	mv -f "$2.install-tmp" "$2"
}

use_opencomposite() {
	[ -f "$copy/bin/linux64/vrclient.so" ] || die "OpenComposite isn't installed in $copy: run install.sh without options first."
	py use "$paths_file" "$copy"
	say "OpenVR games now use OpenComposite ($copy)."
	say "Starting SteamVR from Steam switches them back to SteamVR; $copy/install.sh --opencomposite switches again."
}

# steamvr: the copy out of the list and SteamVR first. remove (the uninstall): the copy out of the list, and SteamVR
# first only if the copy was.
use_steamvr() {
	py "$1" "$paths_file" "$copy"
	IFS='	' read -r kind first _ _ <<EOF
$(current)
EOF
	case $kind in
	steamvr) say "OpenVR games now use SteamVR ($first)." ;;
	none) say "OpenComposite isn't in $paths_file, which names no other runtime." ;;
	*) say "OpenComposite isn't in $paths_file; OpenVR games use $first." ;;
	esac
}

status() {
	# A paths file that can't be read: current says why, and nothing more is known about it.
	state=$(current) || state=unreadable
	IFS='	' read -r kind first listed exists <<EOF
$state
EOF
	say "OpenVR paths file: $paths_file$([ "$exists" = missing ] && printf ' (none yet)')"
	case $kind in
	opencomposite) say "OpenVR games use: OpenComposite ($first)" ;;
	steamvr) say "OpenVR games use: SteamVR ($first)" ;;
	other) say "OpenVR games use: $first" ;;
	unreadable) say "OpenVR games use: unreadable (see above)" ;;
	*) say "OpenVR games use: nothing set" ;;
	esac
	if [ -f "$copy/bin/linux64/vrclient.so" ]; then
		version=$(installed_version)
		[ -n "$version" ] || version="(a build that is not a release)"
		say "Installed: OpenComposite $version in $copy$([ "$listed" = unlisted ] && printf ', not in the paths file')"
	else
		say "Installed: no ($copy)"
	fi
	runtime=$(openxr_runtime)
	if [ -n "$runtime" ]; then
		say "OpenXR runtime: $runtime"
	else
		say "OpenXR runtime: none found. OpenComposite needs your headset's OpenXR runtime to be the active one."
	fi
}

# The active OpenXR runtime's manifest, as the OpenXR loader finds it outside the Steam Runtime.
openxr_runtime() {
	if [ -n "${XR_RUNTIME_JSON:-}" ]; then
		say "$XR_RUNTIME_JSON"
		return
	fi
	dirs="${XDG_CONFIG_HOME:-$home/.config}:${XDG_CONFIG_DIRS:-/etc/xdg}:/etc"
	old_ifs=$IFS
	IFS=:
	for dir in $dirs; do
		if [ -e "$dir/openxr/1/active_runtime.json" ]; then
			IFS=$old_ifs
			say "$dir/openxr/1/active_runtime.json"
			return
		fi
	done
	IFS=$old_ifs
}

# A launch option's value, in double quotes: Steam runs the launch options with sh, which would split it at a space.
quote() {
	printf '"%s"' "$(printf '%s' "$1" | sed 's/[\\"$`]/\\&/g')"
}

launch_options() {
	manifest=$(openxr_runtime)
	if [ -z "$manifest" ]; then
		say "No active OpenXR runtime found: set your headset's runtime as the active one (its own app or docs say how)."
		return
	fi
	if [ $target = flatpak ]; then
		say "Flatpak Steam: your OpenXR runtime ($manifest) has to be visible inside the Flatpak; see the README's"
		say "\"Linux-specific info\". install.sh doesn't work out launch options for it."
		return
	fi
	resolved=$(readlink -f "$manifest")
	options=
	# Inside the Steam Runtime, the home folder is as it is, the host's /usr and /etc are under /run/host, and other
	# folders aren't there unless named.
	case $manifest in
	"$home"/*) in_home=1 ;;
	*) in_home=0 ;;
	esac
	library=$(py runtime-library "$resolved")
	case $resolved in
	"$home"/*) [ $in_home = 1 ] || options="XR_RUNTIME_JSON=$(quote "$resolved")" ;;
	/usr/* | /etc/*) options="XR_RUNTIME_JSON=$(quote "/run/host$resolved")" ;;
	*)
		# The manifest's folder, and its library's if that is elsewhere (a library_path with a / is relative to the
		# manifest), separated by ":".
		folders=$(dirname "$resolved")
		case $library in
		*/*) library_file=$(cd "$folders" && readlink -f "$library") || library_file= ;;
		*) library_file= ;;
		esac
		case ${library_file%/*} in
		"" | "$folders" | "$folders"/* | "$home"/* | /usr/* | /etc/*) ;;
		*) folders=$folders:${library_file%/*} ;;
		esac
		options="XR_RUNTIME_JSON=$(quote "$resolved") PRESSURE_VESSEL_FILESYSTEMS_RO=$(quote "$folders")"
		;;
	esac
	case $library in
	/usr/*) say "Note: $resolved names its library by an absolute path ($library), which games in the Steam Runtime may not find (there, /usr is the runtime's own)." ;;
	esac
	case "$resolved $library" in
	*wivrn* | *WiVRn*) options="${options:+$options }PRESSURE_VESSEL_FILESYSTEMS_RW=\$XDG_RUNTIME_DIR/wivrn/comp_ipc" ;;
	*monado* | *Monado*) options="${options:+$options }PRESSURE_VESSEL_FILESYSTEMS_RW=\$XDG_RUNTIME_DIR/monado_comp_ipc" ;;
	esac
	say "OpenXR runtime: $manifest$([ "$resolved" != "$manifest" ] && printf ' -> %s' "$resolved")"
	if [ -n "$options" ]; then
		say "Games in the Steam Runtime (most Proton games) need these launch options (Steam: the game's Properties,"
		say "General, Launch options) to reach it:"
		say ""
		say "    $options %command%"
	else
		say "Games in the Steam Runtime should find it without launch options."
	fi
	say ""
	say "This is worked out from the runtime's files and may not cover yours; the README's \"Linux-specific info\" explains."
}

fetch() {
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL --proto =https --proto-redir =https -H "User-Agent: OpenComposite-installer" "$1"
	elif command -v wget >/dev/null 2>&1; then
		wget -q -O - --header "User-Agent: OpenComposite-installer" "$1"
	else
		die "--update needs curl or wget."
	fi
}

update() {
	command -v sha256sum >/dev/null 2>&1 || die "--update needs sha256sum (coreutils)."
	installed=
	[ -f "$copy/bin/linux64/vrclient.so" ] && installed=$(installed_version)
	releases=$(fetch "https://api.github.com/repos/$REPOSITORY/releases?per_page=30") ||
		die "Couldn't ask GitHub for the releases."
	newest=$(printf '%s' "$releases" | py newest "$installed" "$pre" "$ARCHIVE")
	if [ -z "$newest" ]; then
		say "No newer release${installed:+ than $installed}$([ $pre = 0 ] && printf ' (--pre includes pre-releases)')."
		return
	fi
	IFS='	' read -r version url sha <<EOF
$newest
EOF
	say "Downloading OpenComposite $version"
	work=$(mktemp -d)
	trap 'rm -rf "$work"' EXIT
	# sh runs the EXIT trap on exit, not when killed by a signal.
	trap 'exit 130' INT TERM HUP
	fetch "$url" >"$work/$ARCHIVE" || die "Couldn't download $url."
	actual=$(sha256sum "$work/$ARCHIVE" | cut -d' ' -f1)
	[ "$actual" = "$sha" ] || die "$ARCHIVE was refused: its SHA-256 is $actual, but GitHub lists $sha."
	tar -xzf "$work/$ARCHIVE" -C "$work"
	sh "$work/OpenComposite/install.sh" --no-switch "$target_flag"
}

case $command in
install)
	copy_files
	use_opencomposite
	other_steam
	say ""
	launch_options
	;;
copy) copy_files ;;
use) use_opencomposite ;;
steamvr) use_steamvr steamvr ;;
uninstall)
	use_steamvr remove
	if [ -d "$copy" ]; then
		rm -rf "$copy"
		say "Deleted $copy."
	fi
	;;
status)
	status
	other_steam
	;;
update) update ;;
launch) launch_options ;;
esac
