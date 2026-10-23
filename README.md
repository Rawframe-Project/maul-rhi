# Maul RHI

One GPU API in C over Vulkan, Direct3D 12, Metal and WebGPU, with a frame graph that makes hazards impossible to write.

[![CI](https://github.com/Rawframe-Project/maul-rhi/actions/workflows/ci.yml/badge.svg)](https://github.com/Rawframe-Project/maul-rhi/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/github/license/Rawframe-Project/maul-rhi)](LICENSE)
[![Release](https://img.shields.io/github/v/release/Rawframe-Project/maul-rhi)](https://github.com/Rawframe-Project/maul-rhi/releases)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/Rawframe-Project/maul-rhi/badge)](https://scorecard.dev/viewer/?uri=github.com/Rawframe-Project/maul-rhi)

A game, an engine or an editor that wants to run on every GPU writes its renderer four times, or against a layer that hides too much or too little. Maul RHI is that layer, drawn tightly around what the four APIs share:

- **Adapters and devices** that grant exactly the features and limits a program asks for, so a program that runs on one adapter runs on any that grants the same.
- **Buffers, textures, samplers, shader containers and pipelines**, bound through binding tables and a root block on every API, and through bindless heaps where the GPU has them.
- **A frame graph** as the only way work reaches the GPU. Passes declare what they touch; the library culls, plans every barrier and aliases transient memory. The API has no barriers, fences or semaphores to get wrong.
- **Surfaces and presentation** from the native handles of any window library, with HDR formats where the platform has them.
- **Native doors** for OpenXR and vendor upscalers (FSR, DLSS, XeSS, MetalFX), and a driver interface for platforms whose APIs are closed.

Every call is checked by WebGPU's rules on every driver, so a frame that records anywhere records on the web. The library starts no thread and takes no callbacks: whatever finishes later is answered once in a queue the program drains. All memory comes from the program's allocator when an instance or device is made. Shaders are compiled offline into containers that carry every API's code; nothing is compiled at run time.

## Quick start

```sh
git clone https://github.com/Rawframe-Project/maul-rhi.git
cd maul-rhi
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -LE integration
./build/sample_triangle
```

The tests labeled `integration` and the samples need a GPU, or a software rasterizer such as lavapipe; without one they report themselves skipped. Install the library with `cmake --install build --prefix <prefix>` and use it from CMake:

```cmake
find_package(maul-rhi REQUIRED CONFIG)
target_link_libraries(app PRIVATE maul-rhi::maul-rhi)
```

or through pkg-config as `maul-rhi`. [Getting started](docs/user/getting-started.md) opens a first device.

## Platforms

| Platform | Driver |
|---|---|
| Linux, Android | Vulkan 1.3 |
| Windows | Direct3D 12 (or Vulkan) |
| macOS, iOS | Metal |
| The web | WebGPU, with Emscripten or Clang's `wasm32-wasi` |

The library builds with GCC 14 or Clang 19 and newer, and with `clang-cl` on Windows. Its public headers are C17 and C++17. CI builds and tests every platform, on lavapipe, WARP, the hosted Macs' GPUs, the Android emulator, the iOS simulator and headless Chrome. Present timing is not granted by any driver yet; on Metal, neither are timestamps nor bindless heaps.

## Documentation

- [The documentation site](https://rawframe-project.github.io/maul-rhi/), with the API reference and the per-API mappings generated from the contract.
- [User documentation](docs/README.md): one page per part of the library.
- [How it works](docs/internals/overview.md) and [how to develop it](docs/operations/development.md).
- [Changelog](CHANGELOG.md).

Maul RHI belongs to the Maul family of standalone C libraries, which share one set of conventions. [Maul Window](https://github.com/Rawframe-Project/maul-window) makes the windows it presents to, though any window library works.

## Contributing

Issues are welcome. Pull requests are accepted for issues a maintainer has approved; see [CONTRIBUTING.md](https://github.com/Rawframe-Project/.github/blob/main/CONTRIBUTING.md). The project is developed with AI coding agents, directed and reviewed by its maintainers; outside contributions follow the [AI policy](https://github.com/Rawframe-Project/.github/blob/main/AI_POLICY.md).

## License

MIT; see [LICENSE](LICENSE). The Khronos headers in `khronos/` are under the Apache 2.0 and MIT licenses, and the DirectX headers in `directx/` under the MIT license; their READMEs give the details.
