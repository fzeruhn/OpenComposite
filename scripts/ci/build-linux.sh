#!/bin/sh
# Builds OpenComposite's Linux library the way the release workflow (.github/workflows/build.yml) does: a clean Release
# build, warnings as errors, with the crash backtrace (libunwind, built in). Needs the packages of
# scripts/ci/ubuntu.dockerfile.
#
#   scripts/ci/build-linux.sh [--tests] x64|x86 [build-dir] [version]
#
# --tests also builds the tests (OC_BUILD_TESTS) and runs the unit tests.
#
# x64 makes <build-dir>/bin/linux64/vrclient.so; x86 (scripts/ci/x86-toolchain.cmake) makes <build-dir>/bin/vrclient.so.
set -eu

# Take --tests out of the arguments, wherever it is
tests=0
for a do
	shift
	if [ "$a" = --tests ]; then
		tests=1
	else
		set -- "$@" "$a"
	fi
done

root=$(cd "$(dirname "$0")/../.." && pwd)
arch=${1:-}
case $arch in
x64) lib=bin/linux64/vrclient.so class=02 ;;
x86) lib=bin/vrclient.so class=01 ;;
*)
	echo "usage: $0 x64|x86 [build-dir] [version]" >&2
	exit 2
	;;
esac
out=${2:-$root/build/linux-$arch}
version=${3:-"$(git -C "$root" rev-parse HEAD) (local build)"}

set -- -S "$root" -B "$out" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DERROR_ON_WARNING=ON -DOC_BACKTRACE=ON \
	"-DOC_VERSION=$version" "-DOC_BUILD_TESTS=$([ $tests = 1 ] && echo ON || echo OFF)"
if [ "$arch" = x86 ]; then
	set -- "$@" "-DCMAKE_TOOLCHAIN_FILE=$root/scripts/ci/x86-toolchain.cmake"
fi

rm -rf "$out"
cmake "$@"
# -k: report every compile error, not only the first.
cmake --build "$out" --target OCOVR -j "$(nproc)" -- -k
if [ $tests = 1 ]; then
	cmake --build "$out" -j "$(nproc)" -- -k
	ctest --test-dir "$out" -L unit --output-on-failure
fi

# The library is there and of the right word size (ELF class byte: 01 32-bit, 02 64-bit).
if [ ! -f "$out/$lib" ]; then
	echo "The build made no $out/$lib." >&2
	exit 1
fi
if [ "$(od -An -tx1 -j4 -N1 "$out/$lib" | tr -d ' ')" != "$class" ]; then
	echo "$out/$lib isn't a $arch library." >&2
	exit 1
fi
# The entry points of a system-wide install (vrclient) and of a per-game one (in place of libopenvr_api.so).
for symbol in VRClientCoreFactory VR_InitInternal2; do
	if ! nm -D --defined-only "$out/$lib" | grep -q " $symbol\$"; then
		echo "$out/$lib doesn't export $symbol." >&2
		exit 1
	fi
done

echo "OpenComposite $version, $arch"
echo "  $out/$lib ($(($(wc -c <"$out/$lib") / 1024)) KB)"
echo "  needs $(objdump -T "$out/$lib" | grep -o 'GLIBC_[0-9.]*' | sort -u -V | tail -n 1) or later"
