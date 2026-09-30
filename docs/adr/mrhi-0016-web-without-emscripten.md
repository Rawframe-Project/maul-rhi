# mrhi-0016. The web without Emscripten

Status: Accepted

## Context

The WebGPU driver (mrhi-0003) is built with Emscripten: its browser side
is JavaScript in the C files as `EM_JS` functions, which Emscripten
compiles into the program, and its conformance suite sleeps through
Emscripten's JSPI support while the browser settles its promises.

Programs built for the web with a plain WebAssembly toolchain (Clang's
`wasm32-wasi` and wasi-libc, as the Rawframe engine and its client are)
have neither. Such a program is a module the page instantiates; nothing
compiles JavaScript into it. Maul Window met the same need in mwin-0022;
this record makes the same choice for Maul RHI.

## Decision

- **One set of JavaScript.** The `EM_JS` functions stay the only copy of
  the browser's side. `src/web_js.h`, the one file of the library that
  includes Emscripten's headers, defines `EM_JS` for a build without
  Emscripten as the declaration of a function imported under its own
  name from the module `env`. `tools/gen_web_glue.py` reads the same C
  files and writes `maul-rhi.mjs`, whose one export,
  `maulRhiImports(exports, module)`, returns those functions for the
  page to put among its `env` imports. `exports` is a function returning
  the instance's exports, which exist only once the imports are given;
  `module`, if the page passes one, is the object the functions keep
  their state in as Emscripten's `Module` (the driver's `mrhiGpu`, whose
  kept WebGPU errors a page or runner reads). The build writes the file
  beside the library and names it in the library's `MAUL_RHI_WEB_GLUE`
  property.
- **Names.** Every function in `env` meets the program's and every
  other library's there, so each starts with the library's prefix
  (`mrhiJs...`); they were `Js...`.
- **The runtime the functions use** is a few lines at the top of the
  generated file, as in mwin-0022: views of memory made again when it
  grows, `UTF8ToString`, `stringToUTF8`, `lengthBytesUTF8`, the function
  table, and `Module`.
- **Builds.** `cmake/wasm32-wasi.cmake` is the toolchain; a WASI build is
  a web build: no native driver, the WebGPU driver on.
- **Tests.** The suites run under Node's WASI (`test/wasi_run.mjs`) with
  the generated imports, where the driver finds no adapter as it does
  under Node with Emscripten. The conformance suite is a reactor the
  web runner loads in headless Chrome: the page gives it a minimal WASI
  (output, the environment, clocks, random bytes, exit), the generated
  imports, a sleep through JSPI's `WebAssembly.Suspending`, and the
  canvas's resizing, and calls its `main` through
  `WebAssembly.promising`. A CI job, `web-wasi`, builds and runs both.

## Consequences

- The JavaScript cannot drift between the two builds: there is one copy,
  and the generator makes the second form of it. A construct the
  generator does not understand fails the glue's build, not the page.
- A page built without Emscripten loads `maul-rhi.mjs` and exports its
  table: a few lines in a program's own loader, which a plain
  WebAssembly program has anyway.
- Emscripten builds are unchanged but for the functions' names.
