#!/bin/sh
# Tries the Linux archive's install.sh on a scratch home folder: install, switch both ways, keep the owner's files,
# uninstall, and the paths file's other keys kept throughout. No network, no Steam.
#
#   scripts/ci/test-install.sh dist/OpenComposite-linux.tar.gz
set -eu

archive=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() {
	echo "test-install.sh: $*" >&2
	exit 1
}

mkdir -p "$work/unpacked" "$work/home folder/.config/openvr" "$work/home folder/.local/share/Steam"
tar -xzf "$archive" -C "$work/unpacked"
installer=$work/unpacked/OpenComposite/install.sh
[ -x "$installer" ] || fail "the archive has no executable OpenComposite/install.sh."

export HOME="$work/home folder"
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_CONFIG_DIRS XR_RUNTIME_JSON
paths=$HOME/.config/openvr/openvrpaths.vrpath
copy=$HOME/.local/share/OpenComposite
steamvr=$HOME/.local/share/Steam/steamapps/common/SteamVR
cat >"$paths" <<EOF
{
	"config" : [ "$HOME/.local/share/Steam/config" ],
	"external_drivers" : null,
	"jsonid" : "vrpathreg",
	"log" : [ "$HOME/.local/share/Steam/logs" ],
	"runtime" : [ "$steamvr", "/opt/other-runtime" ],
	"version" : 1
}
EOF
cp "$paths" "$work/original"

# runtime_list: the "runtime" entries, one per line. same_keys: every key but "runtime" as in the original.
runtime_list() {
	python3 -c 'import json, sys; print("\n".join(json.load(open(sys.argv[1]))["runtime"]))' "$paths"
}
same_keys() {
	python3 -c '
import json, sys
a, b = (json.load(open(f)) for f in sys.argv[1:])
a.pop("runtime"); b.pop("runtime")
sys.exit(a != b)' "$work/original" "$paths" || fail "a key other than \"runtime\" changed."
}
expect() {
	actual=$(runtime_list)
	[ "$actual" = "$1" ] || fail "after $2, \"runtime\" is
$actual
and not
$1"
	same_keys
}

sh "$installer" >"$work/log" || fail "install failed: $(cat "$work/log")"
for file in bin/linux64/vrclient.so bin/vrclient.so bin/version.txt SOURCE.txt install.sh LICENSE.txt; do
	[ -f "$copy/$file" ] || fail "the install has no $file."
done
expect "$copy
$steamvr
/opt/other-runtime" "install"
cmp -s "$paths.bak" "$work/original" || fail "openvrpaths.vrpath.bak isn't the file as it was."

status=$(sh "$copy/install.sh" --status)
case $status in
*"OpenVR games use: OpenComposite"*) ;;
*) fail "--status doesn't say OpenComposite: $status" ;;
esac

echo "haptics=off" >"$copy/bin/opencomposite.ini"
sh "$installer" --no-switch >/dev/null
[ -f "$copy/bin/opencomposite.ini" ] || fail "installing again deleted bin/opencomposite.ini."

sh "$copy/install.sh" --steamvr >/dev/null
expect "$steamvr
/opt/other-runtime" "--steamvr"

sh "$copy/install.sh" --opencomposite >/dev/null
expect "$copy
$steamvr
/opt/other-runtime" "--opencomposite"

sh "$copy/install.sh" --uninstall >/dev/null
expect "$steamvr
/opt/other-runtime" "--uninstall"
[ ! -e "$copy" ] || fail "--uninstall left $copy."

echo "install.sh: install, status, reinstall, --steamvr, --opencomposite and --uninstall work."
