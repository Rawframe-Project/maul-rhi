// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs a web test in headless Chrome with WebGPU (mrhi-0003): serves the
// test's directory from localhost, a secure context, opens a page with
// a canvas (#mrhi-canvas) that loads the test, prints the test's output,
// and exits with the test's status. A page error, an error the browser
// logs, or a WebGPU error the driver kept fails it. Puppeteer
// comes from MRHI_NODE_MODULES; without it the test is skipped (77).
//
// usage: node web_runner.cjs <test.js>

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
const environment = Object.fromEntries(
    Object.entries(process.env).filter(([name]) => name.startsWith('MAUL_RHI_')));
const root = path.dirname(script);
// The test's output reaches the runner through the console, its exit
// status as a line of its own; the MAUL_RHI_ variables of the runner's
// environment reach its getenv.
const page = `<!doctype html><html><head><meta charset="utf-8"><link rel="icon" href="data:,"></head><body>
<canvas id="mrhi-canvas" width="64" height="48"></canvas>
<script>
var Module = {
    print: text => console.log(text),
    printErr: text => console.log(text),
    onExit: status => console.log('mrhi-test: exit ' + status),
    preRun: [() => Object.assign(Module.ENV, ${JSON.stringify(environment)})],
};
</script>
<script src="${path.basename(script)}"></script></body></html>`;
const types = {'.js': 'text/javascript', '.wasm': 'application/wasm'};

const server = http.createServer((request, response) => {
    if (request.url === '/') {
        response.writeHead(200, {'Content-Type': 'text/html'});
        response.end(page);
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
        const gpu = Module.mrhiGpu;
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
