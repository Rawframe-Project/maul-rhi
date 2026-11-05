#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The library's Windows sources for 32-bit x86, which no runner here
# links: the core and the D3D12 driver, each checked by Clang against
# MinGW-w64's i686 headers and the vendored directx/ headers, with the
# warnings the family's CompilerFlags.cmake gives a 32-bit target, as
# errors. A 32-bit narrowing or a type Windows x86 lacks fails here.
#
#   tools/check_windows32.sh [clang]
#
# It needs MinGW-w64's i686 headers in /usr/i686-w64-mingw32 (Debian's
# and Ubuntu's mingw-w64-i686-dev). Their rpcndr.h is older than the one
# the vendored headers ask for, so the check names the version it has.
set -euo pipefail

clang=${1:-clang-20}
sysroot=/usr/i686-w64-mingw32
[ -f "$sysroot/include/windows.h" ] || {
    echo "no MinGW-w64 i686 headers in $sysroot" >&2
    exit 1
}
cd "$(dirname "$0")/.."

checked=0
failed=0
for file in src/*.c src/generated/*.c; do
    case "${file##*/}" in
    vulkan_* | metal_* | webgpu_* | driver_vulkan.c | driver_webgpu.c) continue ;;
    esac
    if ! "$clang" --target=i686-w64-windows-gnu --sysroot="$sysroot" -std=c23 \
        -Iinclude -Isrc -isystem directx -isystem khronos \
        -D__REQUIRED_RPCNDR_H_VERSION__=475 -DMAUL_RHI_D3D12_DRIVER \
        -msse2 -mfpmath=sse -ffp-contract=off -fno-fast-math -fno-math-errno \
        -Wall -Wextra -Wshadow -Wdouble-promotion -Wfloat-conversion -Wmissing-prototypes \
        -Werror -fsyntax-only "$file"; then
        failed=$((failed + 1))
    fi
    checked=$((checked + 1))
done
echo "$checked sources for 32-bit Windows, $failed failed"
[ "$failed" = 0 ]
