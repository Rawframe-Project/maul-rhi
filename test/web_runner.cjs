// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs a web test in headless Chrome with WebGPU (mrhi-0003): serves the
// test's directory from localhost, a secure context, opens a page with
// a canvas (#mrhi-canvas) that loads the test, prints the test's output,
// and exits with the test's status. A page error, or an error the
// browser logs (WebGPU reports invalid calls there), fails it. Puppeteer
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
    const browser = await puppeteer.launch({
        headless: true,
        args: ['--no-sandbox', '--enable-unsafe-webgpu'],
    });
    let failed = false;
    const done = new Promise(resolve => {
        browser.newPage().then(async tab => {
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
        });
        setTimeout(() => {
            console.log('timed out');
            resolve(1);
        }, 600000);
    });
    const status = await done;
    await browser.close();
    server.close();
    if (failed) {
        console.log('the browser logged an error');
    }
    process.exit(failed && status === 0 ? 1 : status);
});
