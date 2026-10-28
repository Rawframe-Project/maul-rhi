#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Installs Puppeteer, which downloads its own headless Chrome, for the
# web tests (test/web_runner.cjs), and points them at it through
# MRHI_NODE_MODULES in $GITHUB_ENV. Without it a web test is skipped,
# which fails a CI cell. Locally, run the cp and npm lines and export the
# variable.
set -euo pipefail

# Puppeteer and its dependencies as test/browser's lockfile pins them, each
# with its integrity hash. Puppeteer 25's Chrome offers WebGPU over its own
# SwiftShader.
modules=${RUNNER_TEMP:-/tmp}/browser
mkdir -p "$modules"
cp test/browser/package.json test/browser/package-lock.json "$modules/"
npm ci --no-audit --no-fund --prefix "$modules" >/dev/null
echo "MRHI_NODE_MODULES=$modules/node_modules" >>"${GITHUB_ENV:?this script is for CI}"
