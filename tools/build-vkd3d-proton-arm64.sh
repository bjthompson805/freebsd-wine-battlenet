#!/bin/sh
# Cross-builds vkd3d-proton as ARM64 Windows DLLs (d3d12.dll, d3d12core.dll)
# with an llvm-mingw toolchain for aarch64-w64-mingw32, the way its
# package-release.sh builds the other architectures.
#
#   build-vkd3d-proton-arm64.sh [vkd3d-proton checkout] [toolchain] [output]
#
# Defaults: ~/code/vkd3d-proton (with its submodules), ~/code/llvm-mingw-aarch64
# (llvm-mingw's build-all.sh with TOOLCHAIN_ARCHS=aarch64), and
# <checkout>/dist-arm64. The DLLs land in <output>/aarch64.
#
# SPDX-License-Identifier: BSD-2-Clause
set -eu

src=${1:-$HOME/code/vkd3d-proton}
tc=${2:-$HOME/code/llvm-mingw-aarch64}
out=${3:-$src/dist-arm64}
build=$out/build.aarch64

[ -x "$tc/bin/aarch64-w64-mingw32-clang" ] ||
	{ echo "no aarch64-w64-mingw32-clang in $tc/bin" >&2; exit 1; }
[ -f "$src/subprojects/dxil-spirv/meson.build" ] ||
	{ echo "$src: run git submodule update --init --recursive" >&2; exit 1; }

cross=$out/cross-aarch64.txt
mkdir -p "$out"
cat > "$cross" <<EOF
[binaries]
c = '$tc/bin/aarch64-w64-mingw32-clang'
cpp = '$tc/bin/aarch64-w64-mingw32-clang++'
ar = '$tc/bin/aarch64-w64-mingw32-ar'
strip = '$tc/bin/aarch64-w64-mingw32-strip'
widl = '$tc/bin/aarch64-w64-mingw32-widl'
windres = '$tc/bin/aarch64-w64-mingw32-windres'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'windows'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF

rm -rf "$build"
meson setup --cross-file "$cross" --buildtype release --strip \
    --prefix "$out" --bindir aarch64 --libdir aarch64 "$build" "$src"
ninja -C "$build" install
ls -l "$out/aarch64/"*.dll
