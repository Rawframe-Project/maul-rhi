#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Downloads the D3D12 Agility SDK's NuGet package, checks its digest and
# unpacks it into out/agility, where the windows-d3d12 preset's
# MAUL_RHI_D3D12_AGILITY points: the build copies its DLLs into D3D12/
# beside the tests. The package is pinned by its SHA-256, not only by
# its version, and runs ahead of the directx/ headers, which stay at the
# newest tag of DirectX-Headers: the tests use nothing the newer SDK
# adds, and test/d3d12_debug.c exports this version's number.
set -euo pipefail

version=1.620.1
digest=a8a494c3e5f687d9be0a465ec8248da70781ab689d888db8806b600422722be6
mkdir -p out
curl -sSfL --retry 3 -o out/agility.zip \
    "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/$version"
echo "$digest  out/agility.zip" | sha256sum -c -
rm -rf out/agility
unzip -q out/agility.zip -d out/agility
