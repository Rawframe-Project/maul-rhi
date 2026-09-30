// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs a test built for wasm32-wasi under Node's WASI
// (cmake/wasm32-wasi.cmake), with the WebGPU driver's imports from the
// maul-rhi.mjs the build wrote beside it (mrhi-0016). Node has no WebGPU,
// so the driver finds no adapter, as it does under Node with Emscripten;
// the suite that needs one runs in a browser (web_runner.cjs).
//
// usage: node wasi_run.mjs <test.wasm> [arguments]

import {readFile} from 'node:fs/promises';
import {dirname, join} from 'node:path';
import {argv, env, exit} from 'node:process';
import {pathToFileURL} from 'node:url';
import {WASI} from 'node:wasi';

const wasi = new WASI({version: 'preview1', args: argv.slice(2), env, returnOnExit: true});
const module = await WebAssembly.compile(await readFile(argv[2]));
let instance = null;
let page = {};
if (WebAssembly.Module.imports(module).some(entry => entry.module === 'env')) {
    const {maulRhiImports} = await import(pathToFileURL(join(dirname(argv[2]), 'maul-rhi.mjs')).href);
    page = maulRhiImports(() => instance.exports);
}
instance = await WebAssembly.instantiate(module, {wasi_snapshot_preview1: wasi.wasiImport, env: page});
exit(wasi.start(instance));
