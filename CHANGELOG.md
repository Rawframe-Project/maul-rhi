# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every data format.

## [0.3.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.2.1...v0.3.0) (2026-10-25)


### Added

* **d3d12:** run async compute and transfer passes on their own queues ([#79](https://github.com/Rawframe-Project/maul-rhi/issues/79)) ([43ac31c](https://github.com/Rawframe-Project/maul-rhi/commit/43ac31c6285df99bd608f08e7886d518a53856fc)), closes [#76](https://github.com/Rawframe-Project/maul-rhi/issues/76)


### Fixed

* **tools:** refuse only a continue block's step as a stale read ([#82](https://github.com/Rawframe-Project/maul-rhi/issues/82)) ([d4b2ee9](https://github.com/Rawframe-Project/maul-rhi/commit/d4b2ee945147280f5448138965b9570a15bd83d3)), closes [#80](https://github.com/Rawframe-Project/maul-rhi/issues/80)

## [0.2.1](https://github.com/Rawframe-Project/maul-rhi/compare/v0.2.0...v0.2.1) (2026-10-25)

### Fixed

- D3D12: after a native pass, a dispatch or draw whose pipeline came
  from the same shader container as one used before the native pass
  ran with no root signature set. The debug layer reported it, and a
  GPU could run the work with whatever arguments it found. Such passes
  now set their root signature again
  ([#78](https://github.com/Rawframe-Project/maul-rhi/pull/78)).

## [0.2.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.1.0...v0.2.0) (2026-10-24)

The core now tells a driver which passes of other classes each pass
must follow, the first step to running async compute and transfer
passes on queues of their own. Drivers built outside the tree must be
rebuilt. A buffer read in two states after a write now takes the
barrier it was missing, which can change how many barriers a frame
needs.

### Added

- `waitFor` in the driver interface: for each pass, the passes of other
  classes it must follow, worked out from every resource use in the
  frame, so a driver can run async compute and transfer passes on
  queues of their own
  ([#66](https://github.com/Rawframe-Project/maul-rhi/pull/66)).
- `frames.md` says how to time compute and copy work: in a
  graphics-class pass, as async compute and transfer passes take no
  timestamps yet
  ([#36](https://github.com/Rawframe-Project/maul-rhi/pull/36)).

### Changed

- **Breaking, for drivers built outside the tree:** the driver
  interface is SPI version 7, and a table of version 6 is refused with
  `mrhi_errorVersion`. `mrhiDriverPass` gains
  `waitFor[MRHI_PASS_CLASSES]`: by class, the latest pass of that class
  the pass must follow, a null id for none and always for its own
  class. A driver that runs every pass on one queue in the frame's
  order may ignore it, as every driver in the tree does. Drivers are
  admitted on `conformance/mustpass/spi-7.txt`, which adds
  `hazards.pass_classes` and `hazards.read_states` to the cases of
  version 6, and may now be handed barriers between two read states of
  a buffer ([#66](https://github.com/Rawframe-Project/maul-rhi/pull/66),
  [#69](https://github.com/Rawframe-Project/maul-rhi/pull/69),
  [#71](https://github.com/Rawframe-Project/maul-rhi/pull/71)).
- **Breaking:** a buffer read in a second read state now takes a
  barrier from the first. Before, the second read could miss the last write, and a later
  write could overtake the first read. `mrhiGetFrameBarriers` lists
  these barriers, and each counts against `frameBarriers`, so a frame
  near that limit can now fail `mrhiCompileFrame` with
  `mrhi_errorCapacity`. Raise `frameBarriers` in the device def by the
  number of state changes between reads of your buffers
  ([#71](https://github.com/Rawframe-Project/maul-rhi/pull/71)).
- A device takes about 415 KB more from the program's allocator when it
  is made, at the default limits, to track which passes of other classes
  each pass follows
  ([#66](https://github.com/Rawframe-Project/maul-rhi/pull/66)).
- The library is built with `-fno-math-errno`, so `sqrt` is one
  rounded instruction on every target, never a C library call
  ([#49](https://github.com/Rawframe-Project/maul-rhi/pull/49),
  [#55](https://github.com/Rawframe-Project/maul-rhi/pull/55)).
- Debug and sanitizer builds trap on a broken internal invariant of the
  frame graph, the heaps, shader containers and the Vulkan driver's
  submissions; Release builds are unchanged
  ([#39](https://github.com/Rawframe-Project/maul-rhi/pull/39),
  [#66](https://github.com/Rawframe-Project/maul-rhi/pull/66),
  [#69](https://github.com/Rawframe-Project/maul-rhi/pull/69)).

### Fixed

- Vulkan: after a frame failed to record, such as a native pass whose
  next command buffer failed to begin, the next frame wrote timestamps
  and queries into query sets it had not reset, and the failed command
  buffer was still ended. A failed begin or end now stops the recording,
  and every recording resets its query sets
  ([#69](https://github.com/Rawframe-Project/maul-rhi/pull/69)).

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
