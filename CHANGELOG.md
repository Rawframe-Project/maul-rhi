# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every data format.

## [0.6.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.5.0...v0.6.0) (2026-10-25)


### Added

* **device:** wait for a device's next notification ([#102](https://github.com/Rawframe-Project/maul-rhi/issues/102)) ([059ca2f](https://github.com/Rawframe-Project/maul-rhi/commit/059ca2f04771e1f3605ba1e515cc8822ffb0a332)), closes [#98](https://github.com/Rawframe-Project/maul-rhi/issues/98)

## [0.5.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.4.1...v0.5.0) (2026-10-25)

### Added

- Passes of every class take timestamps. An async compute or transfer
  pass may name a timestamp set and the queries written at its start
  and end, as a graphics pass could. A pass that declares a query
  resolve access runs after every timed pass before it, and a timed
  pass after every such pass, whatever their classes and queues, so a
  resolve reads the times of the passes before it in the frame's order
  and zero for the others. A pass's end is never before its start, but
  two passes' times are not ordered against each other. WebGPU writes a
  transfer pass's times in empty compute passes. Drivers built outside
  the tree meet timestamps on passes of every class and the new
  `waitFor` edges; the SPI headers do not change, and the new
  conformance case is in `spi-7.txt`
  ([#92](https://github.com/Rawframe-Project/maul-rhi/pull/92)).
- D3D12 runs a timed pass on the compute queue when that queue's
  timestamp frequency matches the direct queue's. A timed async compute
  pass stays on the compute queue wherever an untimed one would run,
  and a timed transfer pass runs there too: it measures the compute
  engine's time and loses its overlap with the copy queue, for that pass
  only. Where the frequencies differ, a timed pass runs on the direct
  queue. The copy queue takes no timed pass
  ([#96](https://github.com/Rawframe-Project/maul-rhi/pull/96)).

### Changed

- The library is built with SSE math on 32-bit x86 and the compiler's
  default trapping math, from the family's shared flags. 64-bit builds'
  tests pass unchanged
  ([#101](https://github.com/Rawframe-Project/maul-rhi/pull/101)).

## [0.4.1](https://github.com/Rawframe-Project/maul-rhi/compare/v0.4.0...v0.4.1) (2026-10-25)

### Fixed

- D3D12: a frame that fails after part of it was queued waits for that
  work before releasing its transients. It used to release them at
  once, so a queue still holding the frame could run it against
  released memory. A failed frame now queues nothing after the first
  wait or signal that fails, and every queue is drained before the
  frame's resources go
  ([#86](https://github.com/Rawframe-Project/maul-rhi/pull/86)).

## [0.4.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.3.0...v0.4.0) (2026-10-25)

### Changed

- **Breaking: no old mustpass lists and no fixed diagnostic codes.**
  The conformance mustpass lists for SPI versions 4 to 6 are gone, so a
  driver built outside the tree against one of those versions has no
  list to be admitted on; the core already refused their tables with
  `mrhi_errorVersion`. Only the current version's list stays. Diagnostic
  codes are no longer promised to stay fixed between releases, and a
  program may see codes renumbered or removed. No code changes value in
  this release
  ([#88](https://github.com/Rawframe-Project/maul-rhi/pull/88)).

## [0.3.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.2.1...v0.3.0) (2026-10-25)

Async compute and transfer passes run on queues of their own on D3D12.

### Added

- D3D12: async compute and transfer passes run on a compute and a copy
  queue of their own, beside the graphics work, each after the passes
  it depends on. A transfer pass that touches only buffers runs on the
  copy queue. An async compute pass, or a transfer pass that touches a
  texture, runs on the compute queue when its textures are only stored
  to, copied, or untouched before. An async compute or transfer pass that
  samples a texture, or uses one a graphics pass left as a target or
  sampled, runs with the graphics work in the frame's order, as every
  pass did before. A frame of graphics passes only runs as before
  ([#79](https://github.com/Rawframe-Project/maul-rhi/pull/79)).

### Changed

- D3D12: a device makes a compute and a copy queue beside its direct
  one, and takes more memory from the program's allocator when it is
  made, for the lists and batches each frame runs on them. A frame's
  work on the compute and copy queues starts only once the frame
  before it has finished
  ([#79](https://github.com/Rawframe-Project/maul-rhi/pull/79)).

### Fixed

- `tools/mrhi_dxil.py` no longer refuses HLSL that steps a loop
  variable in the loop's body and then reads the stepped value; only a
  read after a step in a loop's continue block is refused
  ([#82](https://github.com/Rawframe-Project/maul-rhi/pull/82)).

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
