// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs a web test in headless Chrome with WebGPU (mrhi-0003): serves the
// test's directory from localhost, a secure context, opens a page with
// two canvases (#mrhi-canvas, #mrhi-canvas-2) that loads the test,
// prints the test's output, and exits with the test's status. A page error, an error the browser
// logs, or a WebGPU error the driver kept fails it. Puppeteer
// comes from MRHI_NODE_MODULES; without it the test is skipped (77).
// A test built without Emscripten (mrhi-0016) is a WebAssembly reactor:
// the page gives it a minimal WASI (standard output to the console, the
// environment, the clocks, random bytes and exit), the driver's imports
// from maul-rhi.mjs beside it, a sleep through JSPI and the canvas's
// resizing, and calls its main.
//
// usage: node web_runner.cjs <test.js | test.wasm>

const http = require('http');
const fs = require('fs');
const path = require('path');

let puppeteer = null;
try {
    puppeteer = require(path.join(process.env.MRHI_NODE_MODULES || '', 'puppeteer'));
} catch (error) {
    console.log('puppeteer not found through MRHI_NODE_MODULES: skipped');
    process.exit(77);
}

const script = path.resolve(process.argv[2]);
// A WebAssembly module, by its first bytes: CMake names one with or
// without .wasm, as its version's WASI platform does.
const wasi = fs.readFileSync(script).subarray(0, 4).equals(Buffer.from([0, 0x61, 0x73, 0x6d]));
const environment = Object.fromEntries(
    Object.entries(process.env).filter(([name]) => name.startsWith('MAUL_RHI_')));
const root = path.dirname(script);
// The test's output reaches the runner through the console, its exit
// status as a line of its own; the MAUL_RHI_ variables of the runner's
// environment reach its getenv.
const page = `<!doctype html><html><head><meta charset="utf-8"><link rel="icon" href="data:,"></head><body>
<canvas id="mrhi-canvas" width="64" height="48"></canvas>
<canvas id="mrhi-canvas-2" width="64" height="48"></canvas>
${wasi ? '<script type="module" src="/__wasi.mjs"></script>' : `<script>
var Module = {
    print: text => console.log(text),
    printErr: text => console.log(text),
    onExit: status => console.log('mrhi-test: exit ' + status),
    preRun: [() => Object.assign(Module.ENV, ${JSON.stringify(environment)})],
};
</script>
<script src="${path.basename(script)}"></script>`}</body></html>`;
const types = {'.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm'};

// The page of a test without Emscripten.
const wasiPage = `import {maulRhiImports} from './maul-rhi.mjs';
// The driver keeps its state here, where the runner reads its errors.
globalThis.Module = {};
let instance = null;
const memory = () => instance.exports.memory.buffer;
const view = () => new DataView(memory());
const decoder = new TextDecoder();
const encoder = new TextEncoder();
const environment = Object.entries(${JSON.stringify(environment)}).map(([name, value]) => name + '=' + value);
let line = '';
const system = {
    fd_write(fd, vectors, count, written) {
        let total = 0;
        for (let i = 0; i < count; i++) {
            const at = view().getUint32(vectors + 8 * i, true);
            const length = view().getUint32(vectors + 8 * i + 4, true);
            line += decoder.decode(new Uint8Array(memory(), at, length));
            total += length;
        }
        for (let end = line.indexOf('\\n'); end >= 0; end = line.indexOf('\\n')) {
            console.log(line.slice(0, end));
            line = line.slice(end + 1);
        }
        view().setUint32(written, total, true);
        return 0;
    },
    fd_fdstat_get(fd, out) {
        view().setUint8(out, 2);
        view().setUint16(out + 2, 0, true);
        view().setBigUint64(out + 8, 0n, true);
        view().setBigUint64(out + 16, 0n, true);
        return 0;
    },
    fd_close: () => 0,
    fd_seek: () => 70,
    environ_sizes_get(count, bytes) {
        view().setUint32(count, environment.length, true);
        view().setUint32(bytes, environment.reduce((sum, entry) => sum + encoder.encode(entry).length + 1, 0), true);
        return 0;
    },
    environ_get(pointers, buffer) {
        for (const [index, entry] of environment.entries()) {
            const bytes = encoder.encode(entry);
            view().setUint32(pointers + 4 * index, buffer, true);
            new Uint8Array(memory(), buffer, bytes.length).set(bytes);
            view().setUint8(buffer + bytes.length, 0);
            buffer += bytes.length + 1;
        }
        return 0;
    },
    clock_time_get(id, precision, out) {
        view().setBigUint64(out, BigInt(Math.round(performance.now() * 1e6)), true);
        return 0;
    },
    random_get(at, length) {
        crypto.getRandomValues(new Uint8Array(memory(), at, length));
        return 0;
    },
    proc_exit(status) {
        console.log('mrhi-test: exit ' + status);
        throw new Error('exit ' + status);
    },
};
const wasiImports = new Proxy(system, {get: (target, key) => target[key] || (() => 52)});
const bytes = await (await fetch('./${path.basename(script)}')).arrayBuffer();
({instance} = await WebAssembly.instantiate(bytes, {
    env: Object.assign(maulRhiImports(() => instance.exports, globalThis.Module), {
        mrhiTestSleep: new WebAssembly.Suspending(() => new Promise(resolve => setTimeout(resolve, 1))),
        mrhiTestResizeCanvas: width => {
            document.querySelector('#mrhi-canvas').width = width;
        },
    }),
    wasi_snapshot_preview1: wasiImports,
}));
instance.exports._initialize();
const status = await WebAssembly.promising(instance.exports.main)();
console.log('mrhi-test: exit ' + status);
`;

const server = http.createServer((request, response) => {
    if (request.url === '/') {
        response.writeHead(200, {'Content-Type': 'text/html'});
        response.end(page);
        return;
    }
    if (wasi && request.url === '/__wasi.mjs') {
        response.writeHead(200, {'Content-Type': 'text/javascript'});
        response.end(wasiPage);
        return;
    }
    const file = path.join(root, path.normalize(decodeURIComponent(request.url)));
    if (!file.startsWith(root) || !fs.existsSync(file)) {
        response.writeHead(404);
        response.end();
        return;
    }
    response.writeHead(200, {'Content-Type': types[path.extname(file)] || 'application/octet-stream'});
    fs.createReadStream(file).pipe(response);
});

server.listen(0, async () => {
    // Headless Chrome's default SwiftShader path destroys a device once it
    // presents to a canvas; its Vulkan path presents, here over the
    // SwiftShader Chrome ships, whatever Vulkan drivers the host has.
    const executable = await puppeteer.executablePath();
    const icd = path.join(path.dirname(executable), 'vk_swiftshader_icd.json');
    const browser = await puppeteer.launch({
        headless: true,
        executablePath: executable,
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--enable-features=Vulkan',
               '--use-vulkan=swiftshader', '--use-angle=vulkan'],
        env: Object.assign({}, process.env, {VK_ICD_FILENAMES: icd, VK_DRIVER_FILES: icd}),
    });
    let failed = false;
    const tab = await browser.newPage();
    const done = new Promise(resolve => {
        (async () => {
            tab.on('console', message => {
                const text = message.text();
                const exit = /^mrhi-test: exit (-?\d+)$/.exec(text);
                if (exit) {
                    resolve(Number(exit[1]));
                    return;
                }
                console.log(text);
                if (message.type() === 'error') {
                    failed = true;
                }
            });
            tab.on('pageerror', error => {
                console.log(`page error: ${error.message}`);
                resolve(1);
            });
            // A file the page cannot load, the test's own among them,
            // ends the run at once.
            tab.on('response', response => {
                if (response.status() >= 400) {
                    console.log(`not loaded: ${response.url()} (${response.status()})`);
                    resolve(1);
                }
            });
            await tab.goto(`http://localhost:${server.address().port}/`);
        })();
        setTimeout(() => {
            console.log('timed out');
            resolve(1);
        }, 600000);
    });
    const status = await done;
    // The driver keeps every device's uncaptured WebGPU errors: any fails
    // the test, as validation errors do on Vulkan.
    const errors = await tab.evaluate(async () => {
        const gpu = typeof Module === 'undefined' ? null : Module.mrhiGpu;
        // Devices still closing read their error scopes first.
        for (let waited = 0; gpu && gpu.closing > 0 && waited < 10000; waited += 10) {
            await new Promise(resolve => setTimeout(resolve, 10));
        }
        return (gpu && gpu.errors) || [];
    });
    for (const error of errors) {
        console.log(`WebGPU error: ${error}`);
        failed = true;
    }
    await browser.close();
    server.close();
    if (failed) {
        console.log('the browser logged an error');
    }
    process.exit(failed && status === 0 ? 1 : status);
});
