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

Not released. The first decisions are made and the skeleton builds;
the contract schema, the Vulkan and WebGPU drivers and the conformance
suite follow, then Metal and Direct3D 12.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/mrhi.md`.

## License

MIT; see `LICENSE`.
