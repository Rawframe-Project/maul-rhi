#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Installs Puppeteer, which downloads its own headless Chrome, for the
# web tests (test/web_runner.cjs), and points them at it through
# MRHI_NODE_MODULES in $GITHUB_ENV. Without it a web test is skipped,
# which fails a CI cell. Locally, run the npm line and export the variable.
set -euo pipefail

# Puppeteer 24's Chrome offers WebGPU over its own SwiftShader.
version=24.43.1
modules=${RUNNER_TEMP:-/tmp}/browser
npm install --no-audit --no-fund --prefix "$modules" "puppeteer@$version" >/dev/null
echo "MRHI_NODE_MODULES=$modules/node_modules" >>"${GITHUB_ENV:?this script is for CI}"
