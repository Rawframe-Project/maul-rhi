# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every data format.

## [0.2.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.1.0...v0.2.0) (2026-10-24)


### ⚠ BREAKING CHANGES

* **graph:** a buffer read in a second read state now takes a barrier from the first. mrhiGetFrameBarriers lists it, and it counts against frameBarriers, so a frame near that limit can now fail mrhiCompileFrame with mrhi_errorCapacity. A host near the limit raises frameBarriers in its device def by the number of state changes between reads of its buffers.
* **spi:** the driver SPI moves from version 6 to 7. A driver built outside the tree must be rebuilt against version 7, or the core refuses it with mrhi_errorVersion. mrhiDriverPass gains waitFor, which a driver running every pass on one queue in the frame's order may ignore.

### Added

* **spi:** name the passes of other classes each pass must follow ([#66](https://github.com/Rawframe-Project/maul-rhi/issues/66)) ([32a9d6e](https://github.com/Rawframe-Project/maul-rhi/commit/32a9d6e1a6875a26571b83421ce13995f8f63258))
* **vulkan:** record each pass class in a lane of its own ([#69](https://github.com/Rawframe-Project/maul-rhi/issues/69)) ([1d8fa6d](https://github.com/Rawframe-Project/maul-rhi/commit/1d8fa6dfb26d221826e8529b7cf0f2fee2707203)), closes [#68](https://github.com/Rawframe-Project/maul-rhi/issues/68)


### Fixed

* **graph:** barrier a buffer read in a second state ([#71](https://github.com/Rawframe-Project/maul-rhi/issues/71)) ([cff248f](https://github.com/Rawframe-Project/maul-rhi/commit/cff248fa0bdbeb175e665e57b21fcb3360bd3e3b))

## [0.1.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.0.1...v0.1.0) (2026-10-24)

Bad data from content is now refused apart from bugs in the calling
code. A damaged shader, or one that does not fit a pipeline, returns
the new `mrhi_errorFormat` and counts no misuse, so the misuse count
keeps showing only bugs in the program.

### Added

- `mrhi_errorFormat`: the data is malformed or does not contain what
  was asked for. It counts no misuse; every `mrhi_errorInvalid` still
  counts one. Diagnostic records name the check for both
  ([#22](https://github.com/Rawframe-Project/maul-rhi/pull/22)).

### Changed

- **Breaking:** `mrhiCreateShader` refuses a damaged container with
  `mrhi_errorFormat` instead of `mrhi_errorInvalid`. NULL or misaligned
  bytes stay `mrhi_errorInvalid`
  ([#22](https://github.com/Rawframe-Project/maul-rhi/pull/22)).
- **Breaking:** compute and graphics pipeline creation refuse with
  `mrhi_errorFormat` an entry the shader lacks, a constant id or value
  its reflection does not take, a required constant left unset, and
  more constant values than the shader has constants. A NULL entry,
  NULL values with a count and a constant given twice stay
  `mrhi_errorInvalid`
  ([#22](https://github.com/Rawframe-Project/maul-rhi/pull/22)).
- **Breaking:** graphics pipeline creation refuses with
  `mrhi_errorFormat` state the shader's reflection does not take:
  vertex inputs, fragment outputs and targets, the stage interface,
  fragment depth and the written sample mask. Dual-source blending
  whose fragment entry has no second source is `mrhi_errorFormat` too,
  where it was `mrhi_errorUnsupported`. A def with both a bug and such
  state returns `mrhi_errorInvalid` and counts the bug
  ([#27](https://github.com/Rawframe-Project/maul-rhi/pull/27)).
- **Breaking for drivers built outside the tree:** the driver
  interface is SPI version 6, and `mrhiParseContainer` returns
  `mrhi_errorFormat` for a damaged container. Drivers are admitted on
  `conformance/mustpass/spi-6.txt`, which holds the same cases as
  version 5 ([#22](https://github.com/Rawframe-Project/maul-rhi/pull/22)).

## [0.0.1](https://github.com/Rawframe-Project/maul-rhi/releases/tag/v0.0.1) (2026-10-23)

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
