# Maul RHI

The GPU device layer for games, engines and editors: adapters and
devices, resources and binding, pipelines, a frame graph that records
and submits every pass, surfaces and presentation. Written in C23 with
public headers any C17 or C++17 program can include, drivers over each
GPU API directly, and an MIT license.

It will own:

- instances, adapters and devices, and the capabilities a program
  requests from them;
- buffers, textures, samplers, shader containers and pipelines, bound
  through portable binding tables, with bindless heaps where the GPU
  API has them;
- the frame graph, the only way to record and submit work: barriers
  and transient memory are the library's, so hazards cannot be written;
- surfaces, swapchains, presentation and frame pacing, from the native
  handles a window library such as Maul Window gives.

It owns no windows, scenes, materials or shader compilers: it loads
shader containers built offline. Every request that completes later
answers with exactly one record in a queue the program drains; no
library thread runs, and no callback delivers a result.

## Status

Not released. The contract, the Vulkan, Direct3D 12, WebGPU and Metal
drivers, the conformance suite and the samples are in place; the Metal
driver still lacks heaps, timestamps, present timing and multiview.
Each build has one native driver: Metal on Apple systems, Direct3D 12
on Windows (or Vulkan, with `MAUL_RHI_VULKAN_DRIVER=ON` and
`MAUL_RHI_D3D12_DRIVER=OFF`), Vulkan on the others, WebGPU on the web,
built with Emscripten or with Clang's `wasm32-wasi`
(`-DCMAKE_TOOLCHAIN_FILE=cmake/wasm32-wasi.cmake`), whose page loads
the driver's JavaScript from the `maul-rhi.mjs` the build writes
(mrhi-0016). Android runs the Vulkan driver, built with the NDK
(`-DCMAKE_TOOLCHAIN_FILE=cmake/android-emulator.cmake`) and tested in
the emulator (mrhi-0017).
On Vulkan, an OpenXR runtime's instance and device are adopted
(`maul-rhi/vulkan.h`, mrhi-0018).

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Samples

`samples/` holds small programs, each one file on a shared harness
(`samples/harness.c`): `triangle.c` draws a triangle offscreen and
reads it back, `upload_readback.c` moves bytes and texels through the
GPU and back, `textured.c` draws a textured quad with both binding
tables set, `compute_indirect.c` plans a dispatch on the GPU and runs
it indirectly, `msaa.c` resolves a four-sample target, and `shadow.c`
draws with reversed-Z depth and a shadow map. `hdr_surface.c`,
`two_windows.c` and `present_states.c` present, to X windows through
XCB (`samples/window.c`), to the web runner's canvases, or to
CAMetalLayers outside any window on Metal, and
`device_loss.c` loses its device on purpose and recovers. Each checks
its own result, so CTest runs them as tests on the build's native
driver, and in headless Chrome on the web. They are built unless
`MAUL_RHI_BUILD_SAMPLES` is off; their shader containers are made by
`tools/gen_test_shaders.py`.

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/mrhi.md`.

## License

MIT; see `LICENSE`.
