#!/bin/sh
# Cross-builds DXVK as ARM64 Windows DLLs with an llvm-mingw toolchain for
# aarch64-w64-mingw32. vkd3d-proton needs DXVK's dxgi.dll: Wine's dxgi.dll
# drives Wine's own vkd3d and crashes (in wined3d) on a vkd3d-proton device.
#
#   build-dxvk-arm64.sh [dxvk checkout] [toolchain] [output]
#
# Defaults: ~/code/dxvk (with its submodules), ~/code/llvm-mingw-aarch64 and
# <checkout>/dist-arm64. The DLLs land in <output>/aarch64.
#
# SPDX-License-Identifier: BSD-2-Clause
set -eu

src=${1:-$HOME/code/dxvk}
tc=${2:-$HOME/code/llvm-mingw-aarch64}
out=${3:-$src/dist-arm64}
build=$out/build.aarch64

[ -x "$tc/bin/aarch64-w64-mingw32-clang" ] ||
	{ echo "no aarch64-w64-mingw32-clang in $tc/bin" >&2; exit 1; }
command -v glslangValidator >/dev/null ||
	{ echo "DXVK needs glslangValidator (pkg install glslang)" >&2; exit 1; }

cross=$out/cross-aarch64.txt
mkdir -p "$out"
cat > "$cross" <<EOF
[binaries]
c = '$tc/bin/aarch64-w64-mingw32-clang'
cpp = '$tc/bin/aarch64-w64-mingw32-clang++'
ar = '$tc/bin/aarch64-w64-mingw32-ar'
strip = '$tc/bin/aarch64-w64-mingw32-strip'
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
