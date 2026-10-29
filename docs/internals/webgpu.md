# The WebGPU driver

How the WebGPU driver (`driver_webgpu.c`, `webgpu_*.c`) meets the SPI from inside a browser, with Emscripten or without it.

## JavaScript in the library

The driver calls the browser's WebGPU through `EM_JS` functions compiled into its objects. Each instance keeps its GPU objects and its waiting requests on the JavaScript side under small handles. A promise settles into a queue that the driver's poll drains, so nothing blocks, and a program returns to the page's event loop to see answers.

**Without Emscripten.** Programs built with a plain WebAssembly toolchain (Clang's `wasm32-wasi` and wasi-libc) have no `EM_JS`. The `EM_JS` functions stay the only copy of the browser's side: `src/web_js.h`, the one file that includes Emscripten's headers, defines `EM_JS` for a build without Emscripten as the declaration of a function imported under its own name from the module `env`. `tools/gen_web_glue.py` reads the same C files and writes `maul-rhi.mjs`, whose one export, `maulRhiImports(exports, module)`, returns those functions with a few lines of the runtime they use (views of memory made again when it grows, the string helpers, the function table, `Module`). So the JavaScript cannot drift between the two builds, and a construct the generator does not understand fails the glue's build rather than the page. Every function in `env` meets the program's and every other library's there, so each starts with the library's prefix (`mrhiJs...`).

## Adapters and devices

A search lists the browser's adapter under one handle, so it keeps its id. An adapter without WebGPU's core features, or without immediates, which carry the root block, falls below the floor. Features and limits follow the contract's WebGPU rows, and format capabilities are the floor and what the features add.

A WebGPU adapter makes one device, so a device asks a fresh adapter for its `GPUDevice`, with the granted features and limits (a limit below WebGPU's default is raised by the browser); the opening is answered when the browser settles it. Each device holds a validation error scope for its life, read as it closes, so the web test runner fails on any WebGPU error. Loss is reported by the next poll with the browser's message.

## Objects and pipelines

Objects live on the JavaScript side under small handles. Frames are recorded at submission, so a destroyed object waits there until the next frame submitted after its destruction finishes. A view of one aspect takes the format the browser resolves for it. WebGPU places memory itself, so a declared resource's bytes are an estimate for the frame's report.

A shader is a module over the container's WGSL. A pipeline's layout is the whole container's: a bind group layout per table up to the last one used, empty for a table without bindings, and the root block's bytes as immediates. Pipelines are made with the browser's asynchronous calls and answered by the poll after they settle; one the browser refuses is reported as a platform failure and kept with the device's errors, since the core has already checked it. The pipeline cache is empty and takes back only its own.

## Frames

A frame is recorded at submission into one command encoder, one call into JavaScript per command, and the browser's own synchronization replaces the core's barriers.

- A pass with targets is a render pass. A graphics pass without them is a compute pass, which leaves its GPU compute pass for copies, query resolves and debug labels on the command encoder, and sets its pipeline, bind groups and immediates again in the next one; its end timestamp is then written by a compute pass of its own.
- A binding table is a bind group made at its command in the pipeline's layout.
- Uploads are written into one staging buffer before the frame's commands, so queue order keeps them apart from earlier frames'. Readbacks are copied into a mirror of the ring, mapped after submission and copied into the ring before the frame is reported finished. A frame without readbacks finishes when the queue has done its work. Frames are reported in order.
- Declared resources come from a pool keyed by their descriptors, each its own object, never aliased; one no frame took in the last 8 is destroyed.
- A query set keeps its values across frames, so the queries a frame did not write are cleared to zero after a resolve.
- `mrhiPass_ClearBuffer` is `clearBuffer`. The core's clear of a new texture records nothing, as WebGPU gives every new texture zeros itself. Counted multi-draw, multiview, pipeline statistics, native passes and heaps are never granted.

## Canvases

A surface is the canvas its selector names, with its WebGPU context: first a canvas the program registered in `Module.mrhiCanvases`, then an element of the document where there is one ([surfaces.md](surfaces.md#workers-on-the-web)). A selector naming no canvas, or a canvas without a WebGPU context, is a source the driver cannot use. Colors are the browser's preferred 8-bit format first, then the other, with the sRGB curve, and half floats in linear values, each in Rec. 709 and Display P3, the floats also in extended range through extended tone mapping. It presents in order only, opaque or premultiplied.

Configuring sets the canvas's drawing buffer to the configured size, so the size is the program's own. An acquired image is the context's current texture, out of date once the page resizes the drawing buffer. The browser presents it when the page returns to its event loop, so an image given back is only forgotten.

## Tests

On the web the suites run under Node (with Emscripten) or Node's WASI (`test/wasi_run.mjs`, without), where the driver finds no adapter. The conformance suite and the samples run in headless Chrome with WebGPU (`test/web_runner.cjs`, Puppeteer from `MRHI_NODE_MODULES`) on Chrome's Vulkan path over the SwiftShader it ships, named as the only Vulkan driver, since its default SwiftShader path destroys a device that presents to a canvas. With Emscripten they are built with JSPI, so they sleep while the browser settles promises; without it, the conformance suite is a reactor the runner loads with a minimal WASI, the generated imports, a sleep through `WebAssembly.Suspending`, and calls its `main` through `WebAssembly.promising`. The runner also has a worker mode that transfers both canvases to a dedicated worker, in which the conformance suite runs again (`conformance_worker`). An error the browser logs fails the run.
