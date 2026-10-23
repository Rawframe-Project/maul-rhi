# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every data format.

## [0.0.1](https://github.com/Rawframe-Project/maul-rhi/compare/v0.0.1...v0.0.1) (2026-10-23)


### Documentation

* note the macOS 26 cell's disabled Metal check ([#10](https://github.com/Rawframe-Project/maul-rhi/issues/10)) ([252bf29](https://github.com/Rawframe-Project/maul-rhi/commit/252bf2960bed0c0d026a275f6a9ac7f3f386fa98)), closes [#9](https://github.com/Rawframe-Project/maul-rhi/issues/9)

## [0.0.1](https://github.com/Rawframe-Project/maul-rhi/releases/tag/v0.0.1)

The first release: one GPU API over Vulkan, Direct3D 12, Metal and
WebGPU, with every call checked by WebGPU's rules on every driver.

### Added

- Instances, adapter searches and devices that grant exactly the
  features and limits a program asks for, above a floor every adapter
  meets; format capabilities per adapter.
- Buffers, textures, views, samplers and query sets, with
  generation-checked ids and all memory taken from the program's
  allocator when an instance or device is made.
- Shader containers made offline with SPIR-V, WGSL, DXIL and Metal
  code beside one reflection, read as hostile input, and the tools to
  cook them (`tools/mrhi_container.py`, `mrhi_msl.py`, `mrhi_dxil.py`).
- Compute and graphics pipelines made asynchronously, specialization
  constants, and pipeline caches checked before use.
- The frame graph: transient and imported resources, passes with
  declared accesses, culling, barriers planned per mip, layer and plane,
  transient memory aliased by lifetime, and a readable plan.
- Recording on several threads at once: binding tables, the root
  block, draws, indexed, indirect and counted multi-draw indirect draws,
  dispatches, copies, buffer clears, uploads, readbacks, debug groups,
  occlusion and timestamp queries, and pipeline statistics on Vulkan
  and Direct3D 12.
- Bindless heaps and sealed resources on Vulkan and Direct3D 12;
  multiview on Vulkan, Direct3D 12 and Metal.
- Surfaces from Win32, Wayland, XCB, Android, `CAMetalLayer` and canvas
  handles, with HDR formats where the platform has them, presented at
  submission; the WebGPU driver also runs in a worker.
- Device loss answered for every request and reported, and a simulated
  loss for testing recovery.
- Diagnostics naming the check behind every refused call, a validation
  layer that checks the driver beneath the library, and debug labels a
  build can compile out.
- OpenXR's Vulkan instance, device and swapchain images adopted, and
  native passes for vendor upscalers on Vulkan, Direct3D 12 and Metal.
- Drivers built outside the library against the installed driver
  interface, admitted by the conformance suite's mustpass list.
- A test driver without a GPU for programs' own tests.
- Support for Linux, Android, Windows, macOS, iOS and the web, with
  Emscripten or Clang's `wasm32-wasi`.
