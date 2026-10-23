#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Downloads the D3D12 Agility SDK's NuGet package, the version the
# directx/ headers are, checks its digest and unpacks it into
# out/agility, where the windows-d3d12 preset's MAUL_RHI_D3D12_AGILITY
# points: the build copies its DLLs into D3D12/ beside the tests.
set -euo pipefail

version=1.619.5
digest=0e9bcf32aac9a79343ede9b21e4864950ee54577e3d8e19bfcdf002bb4e9bfd6
mkdir -p out
curl -sSfL --retry 3 -o out/agility.zip \
    "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/$version"
echo "$digest  out/agility.zip" | sha256sum -c -
rm -rf out/agility
unzip -q out/agility.zip -d out/agility
