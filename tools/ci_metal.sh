#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Checks Metal's texture usage validation on the CI runner before the
# tests run under it. On some macOS images it zeroes the samples of a
# correctly made texture, which a program with no library code shows
# (test/metal_usage_check.m). Where it does, the cell runs without that
# one check, through $GITHUB_ENV, and with it again once the platform is
# fixed.
set -euo pipefail

binary=${RUNNER_TEMP:-/tmp}/metal_usage_check
clang -fobjc-arc test/metal_usage_check.m -framework Metal -framework Foundation -o "$binary"
result=$(MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 "$binary")
echo "$result"
case "$result" in
    *broken*)
        echo "::warning::Metal's texture usage validation is broken on this image; the tests run without it"
        echo "MTL_SHADER_VALIDATION_TEXTURE_USAGE=0" >>"${GITHUB_ENV:?this script is for CI}"
        ;;
esac
