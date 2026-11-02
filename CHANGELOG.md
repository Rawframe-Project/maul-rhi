# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every data format.

## [0.21.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.20.0...v0.21.0) (2026-11-02)


### ⚠ BREAKING CHANGES

* **resources:** drivers built outside the tree must be rebuilt for SPI version 16; a texture def or surface config may now list its own format among its view formats.

### Fixed

* **resources:** accept a texture's own format among its view formats ([#386](https://github.com/Rawframe-Project/maul-rhi/issues/386)) ([f23967d](https://github.com/Rawframe-Project/maul-rhi/commit/f23967d64b73c06b3799a0d261d089d1d9f32ea8)), closes [#385](https://github.com/Rawframe-Project/maul-rhi/issues/385)

## [0.20.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.19.0...v0.20.0) (2026-11-01)

### Changed

- **Breaking:** `mrhiCreateSampler` refuses an infinite `lodMin` or
  `lodMax` with `mrhi_errorInvalid`, counted as
  `mrhi_diagnosticSamplerDef`, as WebGPU's `createSampler` never takes
  one. Before, Vulkan, D3D12 and Metal took such a sampler, and the
  WebGPU driver passed it on to the browser.
  ([#377](https://github.com/Rawframe-Project/maul-rhi/pull/377))
- **Breaking:** `mrhiCreateView` refuses two views WebGPU refuses, with
  `mrhi_errorInvalid`, counted as `mrhi_diagnosticViewRange`:
  - a 2D array view of a multisampled texture;
  - a view of a transient texture without all of the texture's usages.
    A usage of 0 still resolves to all of them.

  Binding a texture through a table resolves its view the same way, so
  such a binding is refused too. Before, both views were made on every
  driver, and the browser refused the first.
  ([#378](https://github.com/Rawframe-Project/maul-rhi/pull/378))
- **Breaking:** textures follow WebGPU's `createTexture` rules:
  - `mrhiCreateTexture` and `mrhiDeclareTexture` refuse a 3D texture of
    a depth or stencil format with `mrhi_errorInvalid`, counted as
    `mrhi_diagnosticTextureSize`, as for a compressed volume.
  - `mrhiCreateTexture` refuses a transient texture that is not one 2D
    layer of one mip without view formats, with `mrhi_errorInvalid`,
    counted as `mrhi_diagnosticTextureUsage`. A 2D array of one layer
    qualifies.
  - The frame graph makes a declared texture transient only with that
    shape. On Vulkan and Metal, a declared texture of several mips or
    layers, or with a view format in a later entry, now takes frame
    memory where it was kept on chip.

  Before, the core took a 3D depth or stencil texture and checked only
  a transient texture's usages, and the plan checked only the first
  view format entry.
  ([#379](https://github.com/Rawframe-Project/maul-rhi/pull/379))

### Fixed

- The WebGPU driver and Metal surfaces read every view format entry, as
  the contract says unused entries may be anywhere. Before, WebGPU
  stopped at the first unused entry, so a view in the sRGB twin listed
  after one was refused by the browser, for textures and canvases alike.
  A Metal surface's drawables were framebuffer-only when the first entry
  was unused, whatever the others named.
  ([#381](https://github.com/Rawframe-Project/maul-rhi/pull/381))

## [0.19.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.18.0...v0.19.0) (2026-11-01)

### Changed

- **Breaking:** every draw checks the device's `tablesPlusVertexBuffers`
  limit, as WebGPU checks `maxBindGroupsPlusVertexBuffers` at each draw.
  The highest table set plus one and the highest vertex buffer slot set
  plus one must together stay within it, even for slots the pipeline
  does not read. Before, the core checked the limit only against a
  pipeline's own tables and vertex buffers, when the pipeline was made.
  - Past the device's limit within the adapter's is
    `mrhi_errorCapacity`, past the adapter's `mrhi_errorUnsupported`,
    both naming `mrhi_diagnosticLimitTablesPlusVertexBuffers`.
  - The direct, indexed, indirect and counted draws are checked alike.

  ([#367](https://github.com/Rawframe-Project/maul-rhi/pull/367))

### Fixed

- `mrhiPass_SetViewport` compares a viewport's far edges, `x + width`
  and `y + height`, with the range exactly, as WebGPU does. Before, the
  core added them as floats, so a sum just past the range could round
  onto its edge: on a device with `textureDimension2d` 8192,
  `x = 8191 + 2^-11` with `width = 8192` was accepted. Such a viewport
  is now refused like any other past the range.
  ([#369](https://github.com/Rawframe-Project/maul-rhi/pull/369))
- Each pass starts with its root block zeroed on every driver, as
  `docs/user/shaders.md` promises, and bytes written before the pass
  sets a pipeline are kept for it. Before:
  - WebGPU sent immediates only for bytes a pass wrote, so a pass that
    drew without `mrhiPass_SetRootBlock` failed its frame in Chrome;
  - Vulkan and D3D12 set only the bytes a pass wrote, and a write
    before any pipeline stopped the driver;
  - Metal zeroed the block once per frame, so a pass that wrote none
    saw the last pass's bytes.

  On Vulkan, `rootBlockBytes` is now at most 256, the most a container's
  root block holds. Before, it was the adapter's `maxPushConstantsSize`,
  so a device could ask for up to 4096 bytes no container can use.
  ([#371](https://github.com/Rawframe-Project/maul-rhi/pull/371))

## [0.18.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.17.0...v0.18.0) (2026-11-01)

### Changed

- **Breaking:** `mrhiAddPass` refuses a pass whose color targets' bytes
  per sample pass the device's `colorBytesPerSample` limit, as WebGPU's
  `beginRenderPass` does. Before, the core applied the limit to a
  pipeline's targets but not to a pass, so eight `rgba8unorm` targets,
  64 bytes, were accepted at the default limit of 32.
  - The sum is WebGPU's: the render target bytes of each target's view
    format, each from a multiple of its component alignment, in the
    targets' order, skipping null targets.
  - Past the device's limit within the adapter's is
    `mrhi_errorCapacity`, past the adapter's `mrhi_errorUnsupported`,
    both naming `mrhi_diagnosticLimitColorBytesPerSample` as a pipeline
    past it does.

  ([#353](https://github.com/Rawframe-Project/maul-rhi/pull/353))
- **Breaking:** `mrhiAddPass` refuses a transient texture
  (`mrhi_textureTransient`) where WebGPU's `beginRenderPass` does, with
  `mrhi_errorInvalid` counted as `mrhi_diagnosticPassUses`:
  - as a color target, unless it loads `mrhi_loadClear` and stores
    `mrhi_storeDiscard`;
  - as a depth target, unless its depth does both, and its stencil too
    if the format has one;
  - as a resolve target.

  A transient texture's contents live only in its pass, so nothing can
  be loaded from it or kept in it.
  ([#355](https://github.com/Rawframe-Project/maul-rhi/pull/355))
- **Breaking:** `mrhiAddPass` refuses two resolves that WebGPU's
  `beginRenderPass` refuses, with `mrhi_errorInvalid` counted as
  `mrhi_diagnosticPassUses`:
  - a resolve of a format WebGPU does not resolve: `r32float`,
    `rg32float`, `rgba32float`, `r32uint` and `r32sint`;
  - a resolve into a 3D texture's slice.

  A multisampled target of such a format, without a resolve, stays
  allowed where the adapter reports it, and a 3D slice stays a valid
  target. Before, on an adapter that multisamples `r32uint` or
  `r32sint`, as lavapipe does, the Vulkan driver's resolve broke
  `VUID-VkRenderingAttachmentInfo-imageView-06130`.
  ([#358](https://github.com/Rawframe-Project/maul-rhi/pull/358))
- **Breaking:** a pipeline's constant values convert to their
  constants' types as WebGPU converts them. Before,
  `mrhiCreateComputePipeline` and `mrhiCreateGraphicsPipeline` refused
  each of these with `mrhi_errorFormat`:
  - a boolean other than 0 or 1; any finite value is now taken, true
    when it is not 0;
  - an integer with a fraction, which is now truncated toward zero and
    must then fit its type;
  - a float just above `FLT_MAX` that rounds to it.

  NaN and infinities are still refused.

  Drivers built outside the tree must be rebuilt for
  `MRHI_SPI_VERSION` 15, whose mustpass list is
  `conformance/mustpass/spi-15.txt`. A driver may now be handed an
  integer constant with a fraction, which it must truncate, and a
  boolean other than 0 or 1, which is true.
  ([#361](https://github.com/Rawframe-Project/maul-rhi/pull/361))
- **Breaking:** `mrhiCreateShader` refuses storage textures that
  WebGPU's layout refuses:
  - one read and written (`mrhi_storageReadWrite`) in a format other
    than `r32float`, `r32uint` or `r32sint`, as `mrhi_errorFormat`;
  - one of a format the device does not store
    (`mrhiFormatCaps.storage`), such as an sRGB or depth format, as
    `mrhi_errorUnsupported`.

  `tools/mrhi_container.py` does not check the first rule yet
  ([#365](https://github.com/Rawframe-Project/maul-rhi/issues/365)).
  Until it does, the library refuses such a container when the shader
  is made. A driver built outside the tree may assume both rules, also
  from `MRHI_SPI_VERSION` 15.
  ([#363](https://github.com/Rawframe-Project/maul-rhi/pull/363))

## [0.17.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.16.0...v0.17.0) (2026-11-01)

### Changed

- **Breaking:** `mrhiAddPass` accepts a color target whose resource is
  the null id as a location without a target, as WebGPU accepts a null
  color attachment. Before, it refused one as `mrhi_errorStale`, so a
  pipeline with a target of no format between two targets matched no
  pass.
  - A pass with targets still needs at least one color or depth
    target.
  - A null target that names a resolve target is refused as
    `mrhi_errorInvalid`, counted as `mrhi_diagnosticPassUses`.
  - `mrhiPassPlan.colorStores` reads `mrhi_storeDiscard` at a null
    location.

  The core also leaves trailing empty locations out of what drivers
  see: a pass's trailing null targets, and a pipeline's trailing
  targets without a format. Before, a pipeline `[rgba8unorm, none]`
  drawn in a pass `[t]` broke
  `VUID-vkCmdDraw-colorAttachmentCount-06179` on Vulkan, as the two
  color attachment counts differed.

  Drivers built outside the tree may now see a pass color target of
  resource 0, never the last one, and a pipeline def's last color
  target always has a format. They must be rebuilt for
  `MRHI_SPI_VERSION` 14, whose mustpass list is
  `conformance/mustpass/spi-14.txt`.
  ([#343](https://github.com/Rawframe-Project/maul-rhi/pull/343))

### Fixed

- The Vulkan driver orders each frame's copies into the readback ring
  after every earlier frame's. Before, once the ring wrapped, a later
  frame copied into bytes an earlier frame wrote with no dependency
  between them, and synchronization validation reported
  `SYNC-HAZARD-WRITE-AFTER-WRITE`.
  ([#349](https://github.com/Rawframe-Project/maul-rhi/pull/349))
- The Vulkan driver makes readback bytes visible to the host before
  the program takes them. Before, no barrier to the host stage was
  recorded, so a GPU whose caches the host does not snoop could hand
  `mrhiRequest_TakeReadback` stale bytes. One memory barrier at the end
  of a frame that copied into the ring fixes both.
  ([#349](https://github.com/Rawframe-Project/maul-rhi/pull/349))

## [0.16.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.15.1...v0.16.0) (2026-11-01)

### Changed

- **Breaking:** `mrhiAddPass` refuses the clear values WebGPU refuses:
  a cleared color target whose clear value has a NaN or infinite
  component, or, for `r32uint` and `r32sint`, a component whose
  integer part the 32-bit type cannot hold; and a cleared depth target
  whose `clearDepth` is outside 0 to 1, or NaN. All four color
  components are checked, as the driver converts all four, and
  fractions are dropped as WebGPU drops them. Each refusal is misuse
  counted as `mrhi_diagnosticPassUses`, and a target that is loaded or
  discarded takes any value. Before, the Vulkan driver converted an
  integer target's components with C casts, which are undefined for
  such values. Programs see the refusals, and drivers never receive
  such a value.
  ([#336](https://github.com/Rawframe-Project/maul-rhi/pull/336))

### Fixed

- `mrhiDevice_CompileFrame` no longer refuses a frame within
  `frameAccesses` whose uses cut a texture into a grid, such as one
  whose mips are used one at a time and then its layers. It returned
  `mrhi_errorCapacity` naming `mrhi_diagnosticFrameAccesses`. The
  planner's room now holds the most parts a resource's uses can cut it
  into, and the default device's room shrinks from 16400 boxes to
  14336.
  ([#322](https://github.com/Rawframe-Project/maul-rhi/pull/322))
- The Vulkan driver refuses a swapchain image index past the
  swapchain's images from `vkAcquireNextImageKHR` as
  `mrhi_errorDeviceLost`, and a memory requirement of no bytes or with
  an alignment of 0 or not a power of two as `mrhi_errorPlatform`. A
  release build wrote past an array or handed out overlapping memory.
  ([#317](https://github.com/Rawframe-Project/maul-rhi/pull/317))
- The D3D12 driver refuses a back buffer index past the swapchain's
  buffers as `mrhi_errorDeviceLost`, where a release build read past
  its images.
  ([#319](https://github.com/Rawframe-Project/maul-rhi/pull/319))

## [0.15.1](https://github.com/Rawframe-Project/maul-rhi/compare/v0.15.0...v0.15.1) (2026-10-31)

### Fixed

- The D3D12 driver gives a buffer read as indirect arguments the
  non-pixel shader resource state too, from the start. On the Agility
  SDK 1.620.1 with GPU-based validation, a counted multi-draw for a
  pipeline that reads the vertex information moved its arguments buffer
  from the indirect argument state to both, a legal transition that
  runtime refuses: `Close` returned `E_INVALIDARG` with no debug-layer
  message, and the program lost the frame. Indirect draws and dispatches
  no longer fail there. Nothing in the API, the contract or the SPI
  changes. ([#312](https://github.com/Rawframe-Project/maul-rhi/pull/312))

## [0.15.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.14.0...v0.15.0) (2026-10-31)

### Changed

- **Breaking:** `mrhiPass_WriteBuffer` takes the number of bytes it
  reads as `size_t`, like every other length of bytes in the program's
  memory: `mrhiPass_WriteTexture`'s `byteCount`, `mrhiPass_SetRootBlock`'s
  size, the readback and pipeline cache capacities. Before, it took a
  `uint64_t`, so on wasm32 a program passed a 64-bit count for a 32-bit
  memory length. The offset into the buffer stays `uint64_t`.
  ([#301](https://github.com/Rawframe-Project/maul-rhi/pull/301))
- **Breaking:** `mrhiResourcePlan.usage`, a bare `uint32_t` that held
  either kind of usage bits, is replaced by two typed fields:
  `bufferUsage` (`mrhiBufferUsage`) for a buffer and `textureUsage`
  (`mrhiTextureUsage`) for a texture or a surface image.
  `mrhiResource_GetPlan` fills the one that fits the resource and leaves
  the other at 0.
  ([#303](https://github.com/Rawframe-Project/maul-rhi/pull/303))
- **Breaking:** `mrhiQuerySetDef.type` is `kind`, and its enum
  `mrhiQueryType` is `mrhiQueryKind`, as every other field holding a
  choice is a kind. The values keep their names.
  ([#306](https://github.com/Rawframe-Project/maul-rhi/pull/306))

  Programs see all three changes. Drivers built outside the tree see
  the last, and `MRHI_SPI_VERSION` is 13.

## [0.14.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.13.0...v0.14.0) (2026-10-31)

### Changed

- **Breaking:** every public enumeration type is `int32_t`: kinds,
  modes and choices, as `mrhiResult` is. Sets of bit flags and
  `mrhiStructType`, whose bits mark hints and driver-defined types, are
  `uint32_t`. Before, the 44 enumerations were `uint8_t`, `uint16_t` or
  `uint32_t` each, so struct layouts change. `mrhiFormatCaps.sampleCounts`
  has a type of its own, `mrhiSampleCounts`, with `mrhi_samples1Bit`,
  `mrhi_samples2Bit` and `mrhi_samples4Bit`. Every check of a kind
  refuses -1 and `INT32_MIN` as well as values past the last, and a
  switch over a kind no longer takes an unknown value for a known one:
  before, a view of kind -1 on a 3D texture passed as a 3D view.
  ([#283](https://github.com/Rawframe-Project/maul-rhi/pull/283))
- **Breaking:** counters are named for what they count.
  `mrhiFrameUsage`'s counts are `frameResourceCount`, `framePassCount`,
  `frameAccessCount`, `frameBarrierCount` and `frameIndirectDrawCount`;
  its byte measures keep their names. `mrhiDiagnostic.count` is
  `callsRefused`, and `mrhiInstance_GetDriverFaults` is
  `mrhiInstance_GetDriverFaultCount`.
  ([#286](https://github.com/Rawframe-Project/maul-rhi/pull/286))
- **Breaking:** a take call refused with `mrhi_errorBusy` from inside an
  allocator (`mrhiInstance_TakeDiagnostic`,
  `mrhiInstance_TakeNotification`, `mrhiDevice_TakeDiagnostic`,
  `mrhiDevice_TakeNotification`, `mrhiRequest_TakeReadback`) leaves its
  record, bytes and size as they were. Before, it zeroed them, though
  the call in progress may have been filling them.
  ([#266](https://github.com/Rawframe-Project/maul-rhi/pull/266))

  Programs see all three changes. Drivers built outside the tree see
  the first, and `MRHI_SPI_VERSION` is 12.

### Fixed

- A destroy makes its id stale before the allocator gets the object's
  memory back. Before, `mrhiDestroyShader`, the pipeline destroys and
  `mrhiDestroyHeap` freed memory while the id was still live, so a free
  hook could read a shader's reflection being freed, or record a
  pipeline whose driver object was gone; now such a call gets
  `mrhi_errorStale`.
  ([#276](https://github.com/Rawframe-Project/maul-rhi/pull/276))
- `mrhiSuggestSurfaceColor` with `colorOut` pointing at the caps color
  it picks answers that color. Before, it answered `mrhi_success` with
  an all-zero color. Every output copy now reads its source before it
  clears the output.
  ([#281](https://github.com/Rawframe-Project/maul-rhi/pull/281))
- What a driver reports reaches the program only as the contract lists
  it, in every build, with or without the validation layer:
  - A device loss reason the contract does not list reads as
    `mrhi_lossUnknown`, and the loss message is well-formed UTF-8
    without NUL within `MRHI_LOSS_MESSAGE_BYTES`, ending before a
    character the room cuts, a malformed byte or a NUL.
    ([#287](https://github.com/Rawframe-Project/maul-rhi/pull/287))
  - A surface color with an unknown format, primaries, transfer or
    range is left out of the caps.
    ([#287](https://github.com/Rawframe-Project/maul-rhi/pull/287))
  - An adapter kind the contract does not list reads as
    `mrhi_adapterUnknown`, and an adapter's name is well-formed UTF-8
    within `MRHI_ADAPTER_NAME_BYTES`.
    ([#289](https://github.com/Rawframe-Project/maul-rhi/pull/289))
  - Caps a driver reports as presentable without every floor (FIFO,
    opaque alpha, render target usage, a twin, and 8-bit sRGB in
    Rec. 709) read as an adapter that does not present, in
    `mrhiSurface_GetCaps`, the compatible-surface search and surface
    configuration alike. Before, a Debug build trapped and a Release
    build passed the caps on.
    ([#291](https://github.com/Rawframe-Project/maul-rhi/pull/291))
- The core no longer relies on the validation layer to keep a driver's
  answers in range:
  - A poll that claims more events than its room no longer makes the
    core read past its array.
    ([#289](https://github.com/Rawframe-Project/maul-rhi/pull/289))
  - A pipeline answer for a slot past the budget, a pipeline not
    compiling, or another request is ignored, where it wrote past the
    pipeline slots or flipped a ready pipeline.
    ([#289](https://github.com/Rawframe-Project/maul-rhi/pull/289))
  - A pipeline cache size too large to hold with its envelope's header
    fails `mrhiDevice_GetPipelineCache` with `mrhi_errorPlatform`.
    ([#289](https://github.com/Rawframe-Project/maul-rhi/pull/289))
  - A memory alignment of zero, or one that is not a power of two,
    fails `mrhiDevice_CompileFrame` (and a submission that compiles)
    with `mrhi_errorPlatform`, the frame staying open, instead of
    placing transient resources over each other.
    ([#289](https://github.com/Rawframe-Project/maul-rhi/pull/289))
  - A refused submission's budget is recorded only when it is one of
    the `mrhi_diagnosticSubmit` codes.
    ([#289](https://github.com/Rawframe-Project/maul-rhi/pull/289))
- The validation layer counts the breaches above as driver faults,
  under three new codes: `mrhi_diagnosticDriverLossReport`,
  `mrhi_diagnosticDriverSurfaceCaps` and
  `mrhi_diagnosticDriverSurfaceFloors`, and under the existing
  `mrhi_diagnosticDriverAdapter` for an adapter name that is not UTF-8.
  ([#287](https://github.com/Rawframe-Project/maul-rhi/pull/287),
  [#289](https://github.com/Rawframe-Project/maul-rhi/pull/289),
  [#291](https://github.com/Rawframe-Project/maul-rhi/pull/291))
- On Vulkan, a surface that lists more than 16 colors before its 8-bit
  sRGB pair in Rec. 709, as `VK_EXT_swapchain_colorspace` may, keeps
  that color and presents. The driver reads up to 256 surface formats,
  where it read 64; a pair listed past the 256th is not seen.
  ([#293](https://github.com/Rawframe-Project/maul-rhi/pull/293))

## [0.13.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.12.0...v0.13.0) (2026-10-29)

### Changed

- **Breaking:** the four Take calls, `mrhiInstance_TakeDiagnostic`,
  `mrhiInstance_TakeNotification`, `mrhiDevice_TakeDiagnostic` and
  `mrhiDevice_TakeNotification`, leave zeros in their record when they
  return `mrhi_empty`. Before, the diagnostic takes left the record
  untouched, and a record kept across an empty take could read as a new
  one. A program that read the record after an empty take, expecting
  the one it took before, now reads zeros.
  ([#239](https://github.com/Rawframe-Project/maul-rhi/pull/239))
- **Breaking:** `tools/mrhi_dxil.py` cooks DXIL with `-Gis`, so a shader
  cooked again from the same SPIR-V gives different DXIL. Shader cooks,
  and every library that cooks through it (maul-vfx, maul-ui),
  regenerate their containers when they move to this release. A cook's
  own stamp can record the flags from `mrhi_dxil.DXC_FLAGS`. The cook's
  interface and the container format are unchanged. On Metal, the
  driver's own kernel that clamps counted draws' records compiles in the
  safe math mode, as pipelines already did.
  ([#249](https://github.com/Rawframe-Project/maul-rhi/pull/249))

  Programs see the first change, and shader cooks the second. Drivers
  built outside the tree see neither: `MRHI_SPI_VERSION` stays 11.

### Fixed

- 28 calls on an object clear their outputs on any failure, before they
  check anything: the Take calls, the adapter's and the device's
  getters, `mrhiSurface_GetCaps`, `mrhiPass_IsKept`,
  `mrhiResource_GetPlan`, `mrhiPass_GetPlan`, `mrhiShader_GetInfo` and
  the interop calls of `vulkan.h`, `d3d12.h` and `metal.h`. Before, a
  failure left the outputs as the caller had them: `mrhiAdapter_GetInfo`
  with a stale adapter, or `mrhiResource_GetPlan` before a compile, left
  `*infoOut` and `*planOut` holding a value from before the call. Each
  out-parameter's doc says what a failure leaves: zeros, `false`, 0 or
  NULL, or zeros unless a record is taken.
  ([#239](https://github.com/Rawframe-Project/maul-rhi/pull/239))
- On D3D12, float math in a shader runs as its source writes it, once
  the shader is cooked again with this release. Without `-Gis`, DXC
  marked every float operation fast, then reassociated and folded the
  math, so a cooked shader computed something other than its source and
  other than the same shader on Vulkan, Metal and WebGPU. The library's
  own D3D12 kernels and the containers of its tests and samples are
  cooked again.
  ([#249](https://github.com/Rawframe-Project/maul-rhi/pull/249))
- A graphics pipeline def with several vertex attributes past both the
  `vertexStride` and `vertexAttributes` limits no longer fills the
  device's diagnostic queue with that one call's limit records. Each
  limit is named once per def, so the record of the check that refused
  the def is kept. Before, the program could get `mrhi_errorInvalid` and
  a misuse count with no diagnostic naming the check. The status a def
  gets does not change.
  ([#256](https://github.com/Rawframe-Project/maul-rhi/pull/256))

## [0.12.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.11.0...v0.12.0) (2026-10-29)

### Changed

- **Breaking:** the calls on one object are named `mrhiObject_Verb`, by
  the family's naming rule. The object is the kind of the call's first
  id, or else its root, the instance or the device: `mrhiBeginPass` is
  `mrhiPass_Begin`, and `mrhiGetDeviceMisuse` is `mrhiDevice_GetMisuse`.
  Creation, destruction, defaults and calls on no object keep their
  verb-first names, and so do the calls that make a frame's objects:
  `mrhiDeclareTexture`, `mrhiDeclareBuffer`, `mrhiImportTexture`,
  `mrhiImportBuffer`, `mrhiAddPass` and `mrhiAcquireSurfaceImage`. A call
  that takes the oldest record of a queue has the verb `Take`, as
  `mrhiRequest_TakeReadback` does. `mrhiResultName` and
  `mrhiDiagnosticText` are `mrhiGetResultName` and
  `mrhiGetDiagnosticText`. Old to new:

  | Old | New |
  | --- | --- |
  | `mrhiResultName` | `mrhiGetResultName` |
  | `mrhiDiagnosticText` | `mrhiGetDiagnosticText` |
  | `mrhiGetInstanceMisuse` | `mrhiInstance_GetMisuse` |
  | `mrhiNextInstanceDiagnostic` | `mrhiInstance_TakeDiagnostic` |
  | `mrhiGetDriverFaults` | `mrhiInstance_GetDriverFaults` |
  | `mrhiRequestAdapters` | `mrhiInstance_RequestAdapters` |
  | `mrhiNextInstanceNotification` | `mrhiInstance_TakeNotification` |
  | `mrhiGetAdapters` | `mrhiInstance_GetAdapters` |
  | `mrhiGetAdapterInfo` | `mrhiAdapter_GetInfo` |
  | `mrhiGetAdapterFeatures` | `mrhiAdapter_GetFeatures` |
  | `mrhiGetAdapterLimits` | `mrhiAdapter_GetLimits` |
  | `mrhiGetFormatCaps` | `mrhiAdapter_GetFormatCaps` |
  | `mrhiGetDeviceState` | `mrhiDevice_GetState` |
  | `mrhiGetDeviceFeatures` | `mrhiDevice_GetFeatures` |
  | `mrhiGetDeviceLimits` | `mrhiDevice_GetLimits` |
  | `mrhiGetDeviceBudgets` | `mrhiDevice_GetBudgets` |
  | `mrhiGetDeviceTimestampPeriod` | `mrhiDevice_GetTimestampPeriod` |
  | `mrhiGetDeviceMisuse` | `mrhiDevice_GetMisuse` |
  | `mrhiNextDeviceDiagnostic` | `mrhiDevice_TakeDiagnostic` |
  | `mrhiGetDeviceLossReport` | `mrhiDevice_GetLossReport` |
  | `mrhiSimulateDeviceLoss` | `mrhiDevice_SimulateLoss` |
  | `mrhiSetHeapEntry` | `mrhiHeap_SetEntry` |
  | `mrhiClearHeapEntry` | `mrhiHeap_ClearEntry` |
  | `mrhiSetHeapSampler` | `mrhiHeap_SetSampler` |
  | `mrhiClearHeapSampler` | `mrhiHeap_ClearSampler` |
  | `mrhiGetSurfaceCaps` | `mrhiSurface_GetCaps` |
  | `mrhiConfigureSurface` | `mrhiDevice_ConfigureSurface` |
  | `mrhiUnconfigureSurface` | `mrhiSurface_Unconfigure` |
  | `mrhiBeginFrame` | `mrhiDevice_BeginFrame` |
  | `mrhiDropFrame` | `mrhiDevice_DropFrame` |
  | `mrhiSubmitFrame` | `mrhiDevice_SubmitFrame` |
  | `mrhiWaitFrame` | `mrhiRequest_WaitFrame` |
  | `mrhiNextDeviceNotification` | `mrhiDevice_TakeNotification` |
  | `mrhiWaitDeviceNotification` | `mrhiDevice_WaitNotification` |
  | `mrhiSealResource` | `mrhiResource_Seal` |
  | `mrhiUnsealResource` | `mrhiResource_Unseal` |
  | `mrhiCompileFrame` | `mrhiDevice_CompileFrame` |
  | `mrhiIsPassKept` | `mrhiPass_IsKept` |
  | `mrhiGetFrameBarriers` | `mrhiDevice_GetFrameBarriers` |
  | `mrhiGetResourcePlan` | `mrhiResource_GetPlan` |
  | `mrhiGetPassPlan` | `mrhiPass_GetPlan` |
  | `mrhiGetFrameUsage` | `mrhiDevice_GetFrameUsage` |
  | `mrhiGetShaderInfo` | `mrhiShader_GetInfo` |
  | `mrhiGetPipelineCache` | `mrhiDevice_GetPipelineCache` |
  | `mrhiGetPipelineCacheOutcome` | `mrhiDevice_GetPipelineCacheOutcome` |
  | `mrhiBeginPass` | `mrhiPass_Begin` |
  | `mrhiEndPass` | `mrhiPass_End` |
  | `mrhiTakeReadback` | `mrhiRequest_TakeReadback` |
  | `mrhiDescribeVulkanDevice` | `mrhiInstance_DescribeVulkanDevice` |
  | `mrhiGetVulkanPhysicalDevice` | `mrhiAdapter_GetVulkanPhysicalDevice` |
  | `mrhiGetVulkanQueue` | `mrhiDevice_GetVulkanQueue` |
  | `mrhiGetVulkanDevice` | `mrhiDevice_GetVulkanDevice` |
  | `mrhiGetVulkanTexture` | `mrhiTexture_GetVulkanTexture` |
  | `mrhiSetVulkanPassCommands` | `mrhiPass_SetVulkanCommands` |
  | `mrhiGetD3d12Device` | `mrhiDevice_GetD3d12Device` |
  | `mrhiGetD3d12Texture` | `mrhiTexture_GetD3d12Texture` |
  | `mrhiSetD3d12PassCommands` | `mrhiPass_SetD3d12Commands` |
  | `mrhiGetMetalDevice` | `mrhiDevice_GetMetalDevice` |
  | `mrhiGetMetalTexture` | `mrhiTexture_GetMetalTexture` |
  | `mrhiSetMetalPassCommands` | `mrhiPass_SetMetalCommands` |
  | `mrhiSetGraphicsPipeline` | `mrhiPass_SetGraphicsPipeline` |
  | `mrhiSetComputePipeline` | `mrhiPass_SetComputePipeline` |
  | `mrhiSetBindings` | `mrhiPass_SetBindings` |
  | `mrhiSetVertexBuffer` | `mrhiPass_SetVertexBuffer` |
  | `mrhiSetIndexBuffer` | `mrhiPass_SetIndexBuffer` |
  | `mrhiDraw` | `mrhiPass_Draw` |
  | `mrhiDrawIndexed` | `mrhiPass_DrawIndexed` |
  | `mrhiDispatch` | `mrhiPass_Dispatch` |
  | `mrhiDrawIndirect` | `mrhiPass_DrawIndirect` |
  | `mrhiDrawIndexedIndirect` | `mrhiPass_DrawIndexedIndirect` |
  | `mrhiDrawIndirectCount` | `mrhiPass_DrawIndirectCount` |
  | `mrhiDrawIndexedIndirectCount` | `mrhiPass_DrawIndexedIndirectCount` |
  | `mrhiDispatchIndirect` | `mrhiPass_DispatchIndirect` |
  | `mrhiBeginOcclusionQuery` | `mrhiPass_BeginOcclusionQuery` |
  | `mrhiEndOcclusionQuery` | `mrhiPass_EndOcclusionQuery` |
  | `mrhiBeginStatisticsQuery` | `mrhiPass_BeginStatisticsQuery` |
  | `mrhiEndStatisticsQuery` | `mrhiPass_EndStatisticsQuery` |
  | `mrhiResolveQueries` | `mrhiPass_ResolveQueries` |
  | `mrhiCopyBuffer` | `mrhiPass_CopyBuffer` |
  | `mrhiClearBuffer` | `mrhiPass_ClearBuffer` |
  | `mrhiCopyBufferToTexture` | `mrhiPass_CopyBufferToTexture` |
  | `mrhiCopyTextureToBuffer` | `mrhiPass_CopyTextureToBuffer` |
  | `mrhiCopyTexture` | `mrhiPass_CopyTexture` |
  | `mrhiWriteBuffer` | `mrhiPass_WriteBuffer` |
  | `mrhiWriteTexture` | `mrhiPass_WriteTexture` |
  | `mrhiReadBuffer` | `mrhiPass_ReadBuffer` |
  | `mrhiReadTexture` | `mrhiPass_ReadTexture` |
  | `mrhiSetRootBlock` | `mrhiPass_SetRootBlock` |
  | `mrhiSetViewport` | `mrhiPass_SetViewport` |
  | `mrhiSetScissor` | `mrhiPass_SetScissor` |
  | `mrhiSetBlendConstant` | `mrhiPass_SetBlendConstant` |
  | `mrhiSetStencilReference` | `mrhiPass_SetStencilReference` |
  | `mrhiPushDebugGroup` | `mrhiPass_PushDebugGroup` |
  | `mrhiPopDebugGroup` | `mrhiPass_PopDebugGroup` |
  | `mrhiInsertDebugMarker` | `mrhiPass_InsertDebugMarker` |

  ([#232](https://github.com/Rawframe-Project/maul-rhi/pull/232))
- **Breaking:** the arrays four calls fill end in `Out`:
  `mrhiRequest_TakeReadback`'s and `mrhiDevice_GetPipelineCache`'s
  `bytes` is `bytesOut`, `mrhiInstance_GetAdapters`'s `adapters` is
  `adaptersOut`, and `mrhiDevice_GetFrameBarriers`'s `barriers` is
  `barriersOut`.
  ([#237](https://github.com/Rawframe-Project/maul-rhi/pull/237))

  Programs see both changes. Shader cooks and drivers built outside the
  tree see nothing: `MRHI_SPI_VERSION` stays 11.

### Fixed

- `mrhiRequest_TakeReadback`, `mrhiDevice_GetPipelineCache`,
  `mrhiInstance_GetAdapters` and `mrhiDevice_GetFrameBarriers` clear
  their outputs on any failure: zeros in the array's capacity elements,
  and a count or size of 0. A too-small array is the exception: the call
  still reports the size it needed. Before, a failure left the array and
  the count or size as the caller had them. When the driver failed to
  write a pipeline cache, the array held the part it wrote, behind a
  header never filled in.
  ([#237](https://github.com/Rawframe-Project/maul-rhi/pull/237))

## [0.11.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.10.0...v0.11.0) (2026-10-29)

### Added

- `MRHI_MAX_CONTAINER_SECTIONS` (64), `MRHI_MAX_CONTAINER_RECORDS`
  (4096), `MRHI_MAX_CONTAINER_NAME_BYTES` (256) and
  `MRHI_MAX_CONTAINER_ROOT_BLOCK_BYTES` (256) in `maul-rhi/shader.h`:
  the shader container format's fixed bounds, for shader cooks that
  write their own containers. A container past one is still
  `mrhi_errorFormat`.
  ([#213](https://github.com/Rawframe-Project/maul-rhi/pull/213))
- For tests on the test driver: `mrhiTestFrameLog.recordSum`, the sum
  of every byte of a frame's command records, payloads included.
  ([#200](https://github.com/Rawframe-Project/maul-rhi/pull/200))

### Changed

- **Breaking:** a device's own sizes are called budgets.
  `mrhiDeviceLimits` is `mrhiDeviceBudgets`, the device def member
  `deviceLimits` is `budgets`, `mrhiGetDeviceOwnLimits` is
  `mrhiGetDeviceBudgets`, and `mrhi_diagnosticDeviceLimits` is
  `mrhi_diagnosticDeviceBudgets`. The diagnostic texts of the frame
  refusals name the budget, such as "the device's frameUploadBytes
  budget". `mrhiLimits` and `mrhiGetDeviceLimits`, WebGPU's capability
  limits, keep their names.
  ([#227](https://github.com/Rawframe-Project/maul-rhi/pull/227))
- **Breaking:** `mrhiSurfaceConfig.presentMode` and `alphaMode` take the
  enums `mrhiPresentMode` (`mrhi_presentFifo` 0, `mrhi_presentMailbox`
  1, `mrhi_presentImmediate` 2) and `mrhiAlphaMode` (`mrhi_alphaOpaque`
  0, `mrhi_alphaPremultiplied` 1), where they took the caps' bit sets
  and had to set exactly one bit. A zeroed config asks for fifo and
  opaque. A value past the last mode is `mrhi_errorInvalid`, and a mode
  the caps lack is `mrhi_errorUnsupported`. `mrhiSurfaceCaps` keeps the
  sets, with their bits renamed `mrhi_presentFifoBit`,
  `mrhi_presentMailboxBit`, `mrhi_presentImmediateBit`,
  `mrhi_alphaOpaqueBit` and `mrhi_alphaPremultipliedBit`, each 1
  shifted left by its mode.
  ([#217](https://github.com/Rawframe-Project/maul-rhi/pull/217))
- **Breaking:** byte lengths in memory are `size_t`:
  `mrhiAdapterInfo.nameLength`, `mrhiDeviceLossReport.messageLength`
  and `mrhiSetRootBlock`'s `size`, as every label and name length
  already was. Array counts in defs stay `uint32_t`.
  ([#218](https://github.com/Rawframe-Project/maul-rhi/pull/218))
- **Breaking:** `mrhiClearColor` is `mrhiColor`, with the same members.
  It is the type of `mrhiColorTarget.clear` and of
  `mrhiSetBlendConstant`'s color.
  ([#215](https://github.com/Rawframe-Project/maul-rhi/pull/215))
- **Breaking for drivers built outside the tree:** `MRHI_SPI_VERSION`
  is 11, and the mustpass list is `conformance/mustpass/spi-11.txt`,
  with the same cases. Since 0.10.0, `configureSurface` hands a driver
  the renumbered present and alpha modes; the adapter record and the
  loss report a driver fills hold `size_t` lengths; a pass's color
  targets clear to an `mrhiColor`; and a device def's budgets are its
  `budgets` member. A driver built against 0.10.0 is refused at the
  handshake until it is rebuilt against this release.

  Programs and shader cooks see nothing of the version itself;
  contract_version is 22.
  ([#230](https://github.com/Rawframe-Project/maul-rhi/pull/230),
  [#217](https://github.com/Rawframe-Project/maul-rhi/pull/217),
  [#218](https://github.com/Rawframe-Project/maul-rhi/pull/218),
  [#215](https://github.com/Rawframe-Project/maul-rhi/pull/215),
  [#227](https://github.com/Rawframe-Project/maul-rhi/pull/227))

### Fixed

- A call that fills a struct writes every byte of it when it succeeds,
  its padding as zeros. Before, several getters left in the padding
  whatever the library's store, its stack or the caller's storage
  held, so two calls returning the same values could return different
  bytes. Built with GCC 14, the instance and device notifications, the
  diagnostics and the resource plan showed it.
  ([#225](https://github.com/Rawframe-Project/maul-rhi/pull/225))
- The command records drivers read hold zeros where a payload does not
  fill them, and in each payload's padding. Before, they held whatever
  the caller's allocator memory or the stack held, so the same frame
  recorded over memory that held other bytes handed a driver different
  records.
  ([#200](https://github.com/Rawframe-Project/maul-rhi/pull/200))

### Removed

- **Breaking:** `mrhiGetFrameMemory`. A frame's memory is
  `mrhiFrameUsage.frameMemoryBytes`, read with `mrhiGetFrameUsage`: 0
  until the frame's compile succeeds, then kept until the next frame
  begins, after a submission or a drop too. Where `mrhiGetFrameMemory`
  refused with `mrhi_errorState`, it reads 0.
  ([#216](https://github.com/Rawframe-Project/maul-rhi/pull/216))
- **Breaking:** `MRHI_CONTRACT_VERSION`, which only repeated the library
  version. A program that read it uses `MRHI_VERSION_*` or
  `mrhiGetVersion`.
  ([#214](https://github.com/Rawframe-Project/maul-rhi/pull/214))

## [0.10.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.9.0...v0.10.0) (2026-10-28)

### Added

- `mrhi_errorBusy` (-10). The only program code the library calls is
  the allocator a def gives an instance or a device. A function that
  would change that object, called from inside its allocator, now
  returns `mrhi_errorBusy` and counts one misuse, recorded under the
  new `mrhi_diagnosticAllocatorCall`, where before it ran on an object
  in the middle of a change. That is 7 instance functions and 43 device
  functions; `mrhiDestroyInstance` and `mrhiDestroyDevice` called that
  way count the misuse and destroy nothing. While
  `mrhiConfigureSurface`, `mrhiUnconfigureSurface` or
  `mrhiDestroyDevice` runs the device's allocator, the instance is busy
  too, and `mrhiDestroySurface` is refused while the device the surface
  is configured on is busy. Functions that only read, and pass
  recording calls, are not refused.
  ([#187](https://github.com/Rawframe-Project/maul-rhi/pull/187))
- `mrhi_errorLimit` (-9), for an argument past a fixed bound of the
  library, refused before anything past the bound is read and counted
  as misuse. Each bound is a published constant:
  `MRHI_MAX_LABEL_BYTES`, `MRHI_MAX_COLOR_TARGETS`,
  `MRHI_MAX_INDIRECT_DRAWS`, `MRHI_MAX_NATIVE_PASSES`,
  `MRHI_MAX_TABLE_BINDINGS` and the new `MRHI_MAX_QUERY_SET_QUERIES`
  (4096).
  ([#175](https://github.com/Rawframe-Project/maul-rhi/pull/175))
- One diagnostic code per device limit, `mrhi_diagnosticLimit` and the
  limit's name (125 to 155). Every refusal past a limit records its
  limit's code: the device's limit for capacity, the adapter's for
  unsupported.
  ([#186](https://github.com/Rawframe-Project/maul-rhi/pull/186))
- For tests on the test driver: `mrhiTestAdapter.refuseObjects`, set to
  refuse every object its devices make with `mrhi_errorCapacity` as a
  driver out of memory does, and `mrhiTestFrameLog.stagingSum`, the sum
  of every byte of a frame's staging.
  ([#194](https://github.com/Rawframe-Project/maul-rhi/pull/194),
  [#197](https://github.com/Rawframe-Project/maul-rhi/pull/197))

### Changed

- **Breaking:** results follow the Maul family's shared numbering.
  `mrhi_errorFormat` is -8, `mrhi_errorOutOfDate` is -32 and
  `mrhi_errorDeviceLost` is -33. These refusals move:
  - to `mrhi_errorLimit`, from `mrhi_errorInvalid`: a label past its
    bound, more color targets than the bound on a pass or a graphics
    pipeline, a counted multi-draw's `maxCount` past its bound, and a
    query set past its bound;
  - to `mrhi_errorLimit`, from `mrhi_errorCapacity`: more native passes
    in a frame than the bound, which now counts a misuse;
  - to `mrhi_errorFormat`, counting no misuse: a damaged pipeline cache
    (`mrhiGetPipelineCacheOutcome`), which was `mrhi_errorInvalid`, and
    a shader container with a table past `MRHI_MAX_TABLE_BINDINGS`,
    which was `mrhi_errorUnsupported`.
  
  The bounds' constants are renamed with no aliases: `MRHI_LABEL_BYTES`
  to `MRHI_MAX_LABEL_BYTES`, `MRHI_COLOR_TARGETS` to
  `MRHI_MAX_COLOR_TARGETS`, `MRHI_INDIRECT_DRAWS` to
  `MRHI_MAX_INDIRECT_DRAWS`, `MRHI_NATIVE_PASSES` to
  `MRHI_MAX_NATIVE_PASSES` and `MRHI_TABLE_BINDINGS` to
  `MRHI_MAX_TABLE_BINDINGS`.
  ([#175](https://github.com/Rawframe-Project/maul-rhi/pull/175))
- **Breaking:** a value past one of the device's limits is
  `mrhi_errorCapacity` when the adapter's limit holds it, since a
  device asking for more would take it, and `mrhi_errorUnsupported`
  when not. Neither counts a misuse.
  - Creations that answered unsupported for every such value now answer
    capacity within the adapter's limits: a buffer's or texture's size,
    a heap's entries, a shader container, a graphics pipeline's vertex
    state, color bytes or views, and a surface's size. The same holds
    for a pass's views and the root block while recording.
  - Recording calls that answered `mrhi_errorInvalid` and counted a
    misuse now answer capacity or unsupported: a table index, a vertex
    buffer slot, a binding's offset alignment or size, a heap entry's
    offset alignment, a viewport past the 2D texture limit, and a
    dispatch past the workgroups per dimension.
  - `mrhi_diagnosticVertexBufferSlot` and `mrhi_diagnosticDispatchSize`
    are removed.
  ([#186](https://github.com/Rawframe-Project/maul-rhi/pull/186))
- A reconfiguration that `mrhiConfigureSurface` refuses keeps the
  surface configured as it was. Before, every failed reconfiguration
  left the surface unconfigured. Now only a platform call that fails
  after retiring the old swapchain does: Vulkan retires it whatever
  `vkCreateSwapchainKHR` answers, and Direct3D 12 releases it first, as
  a window holds one swapchain.
  ([#195](https://github.com/Rawframe-Project/maul-rhi/pull/195))
- **Breaking for drivers built outside the tree:** `MRHI_SPI_VERSION`
  is 10, and the mustpass list is `conformance/mustpass/spi-10.txt`.
  - The command payload structs (`mrhiCommandBinding`,
    `mrhiCommandBufferSide`, `mrhiCommandTextureSide`) and the chunk
    header of `mrhiCommandChunk` lose their reserved fields. Each
    payload struct is read from its own 32-byte record, a side is 24
    bytes, and a chunk is 4072 bytes.
  - `configureSurface` may hand back `oldSwapchain` in `swapchainOut`
    when it refuses before its platform call, and the old swapchain
    keeps working. A driver that leaves it 0 behaves as before.

  Programs and shader cooks see nothing of this; contract_version is
  21.
  ([#174](https://github.com/Rawframe-Project/maul-rhi/pull/174),
  [#175](https://github.com/Rawframe-Project/maul-rhi/pull/175),
  [#195](https://github.com/Rawframe-Project/maul-rhi/pull/195))

### Fixed

- A device def whose `uniformOffsetAlignment` or
  `storageOffsetAlignment` is not a power of two is refused as
  `mrhi_errorInvalid` under `mrhi_diagnosticLimitsFloor`. Before, a
  device could ask for 96 on an adapter whose alignment is 64, and a
  binding at offset 96 reached the driver. An adapter whose driver
  reports such an alignment is no longer listed.
  ([#179](https://github.com/Rawframe-Project/maul-rhi/pull/179))
- A graphics pipeline def with more vertex buffers than the limit is
  refused before its attributes are read. Before, an attribute naming a
  buffer past the limit read that buffer, past the end of an array that
  held only the limit's entries.
  ([#167](https://github.com/Rawframe-Project/maul-rhi/pull/167))
- `mrhiCreateDevice` refused for memory leaves the instance as it was.
  Before, it took a request id, so later requests got later ids. With
  the validation layer, it also left a false driver fault in the
  instance's diagnostics.
  ([#192](https://github.com/Rawframe-Project/maul-rhi/pull/192))
- A creation the driver refuses, or a shader whose reflection cannot be
  allocated, leaves its pool as it was, so the next object gets the id
  it would have. Before, the refused slot's generation moved on. This
  covers samplers, buffers, textures, views, query sets, heaps,
  shaders, compute and graphics pipelines, and surfaces.
  ([#194](https://github.com/Rawframe-Project/maul-rhi/pull/194))
- Two staging fixes:
  - Staging hands drivers no bytes the program never wrote. The gaps
    after each texture row and after each upload hold zeros. Before,
    they held what the device's memory held, and the Vulkan,
    Direct3D 12 and Metal drivers copy the staging whole into GPU
    upload memory.
  - An upload no longer takes staging past the frame's region when
    `frameUploadBytes` is not a multiple of 512. Before, the driver was
    handed bytes past the region, and Debug builds trapped.
  ([#197](https://github.com/Rawframe-Project/maul-rhi/pull/197))
- `tools/check_contract_version.py` asks a contract change for a
  version above the latest release's and not below the base's. Before,
  it asked every change to raise the version again, so a second
  contract change in one release failed.
  ([#184](https://github.com/Rawframe-Project/maul-rhi/pull/184))

## [0.9.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.8.0...v0.9.0) (2026-10-27)

### Added

- `mrhiGetFrameUsage(device, &usage)` fills `mrhiFrameUsage` with what
  the frame begun last took of each frame limit: `frameResources`,
  `framePasses`, `frameAccesses`, `frameBarriers`, `frameCommandBytes`,
  `frameUploadBytes` and `frameIndirectDraws`, each named after the
  `mrhiDeviceLimits` member it is measured against, so the headroom is
  the difference from `mrhiGetDeviceOwnLimits`. It can be read while
  the frame is open, once compiled, after a refusal and after the frame
  is submitted or dropped, until the next frame begins; before the
  first frame every field is 0. The library's own zeroing pass is left
  out, and command bytes count in whole 4 KiB chunks.
  ([#161](https://github.com/Rawframe-Project/maul-rhi/pull/161))
- A frame call's `mrhi_errorCapacity` now names the limit it hit in the
  device's diagnostic queue, and counts no misuse. Twelve codes cover
  the frame limits (`mrhi_diagnosticFrameResources`, `FramePasses`,
  `FrameAccesses`, `FrameBarriers`, `FrameCommandBytes`,
  `FrameUploadBytes`, `FrameIndirectDraws`), the readback limits
  (`Readbacks`, `ReadbackBytes`) and `FramesInFlight`, `Notifications`
  and `NativePasses`. A pass that found the command arena or the
  staging full keeps that code, so its later calls and the frame's
  submission are refused under it too. Object limits outside frames
  still refuse without a code.
  ([#161](https://github.com/Rawframe-Project/maul-rhi/pull/161))
- A driver's refusal of a frame's submission names the budget that ran
  out: `mrhi_diagnosticSubmitMemory`, `SubmitDescriptors`,
  `SubmitScratch` or `SubmitCommandLists`, recorded beside the frame
  limits' codes with no misuse counted. Direct3D 12, Vulkan and Metal
  fill it in; WebGPU never refuses for capacity. The validation layer
  counts `mrhi_diagnosticDriverSubmitBudget` when a driver's capacity
  refusal names no submit code or another code, or when a code comes
  with another result. `mrhiTestAdapter.submitBudget` makes the test
  driver run out of a budget at every submission.
  ([#163](https://github.com/Rawframe-Project/maul-rhi/pull/163))

### Changed

- **Breaking for drivers built outside the tree:** `MRHI_SPI_VERSION` is
  9, and `submitFrame` takes a fourth argument,
  `mrhiDiagnosticCode* budgetOut`. A driver sets it to the submit code
  of the budget that ran out when it returns `mrhi_errorCapacity`, and
  leaves it 0 otherwise. The mustpass list is
  `conformance/mustpass/spi-9.txt`. Programs and shader cooks see only
  the new codes; contract_version is 20.
  ([#161](https://github.com/Rawframe-Project/maul-rhi/pull/161),
  [#163](https://github.com/Rawframe-Project/maul-rhi/pull/163),
  [#165](https://github.com/Rawframe-Project/maul-rhi/pull/165))

### Fixed

- A refused upload leaves the staging as it was. Before, its bytes were
  added to the staging anyway, so later uploads in other passes of the
  frame could be refused too.
  ([#161](https://github.com/Rawframe-Project/maul-rhi/pull/161))
- On Vulkan, a command buffer that fails to begin or end, and a command
  pool that fails to reset, name `mrhi_diagnosticSubmitCommandLists`
  instead of `mrhi_diagnosticSubmitMemory`, and the two codes' texts now
  say what each covers.
  ([#165](https://github.com/Rawframe-Project/maul-rhi/pull/165))
- `tools/check_contract_version.py` refuses a base commit whose tree git
  cannot read, instead of passing as if the base had no contract.
  ([#144](https://github.com/Rawframe-Project/maul-rhi/pull/144))

## [0.8.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.7.0...v0.8.0) (2026-10-26)

### Changed

- **Breaking:** every new device texture reads as zeros in its first
  frame, on every driver, as buffers have since 0.7.0. The first frame
  that imports a new texture clears it whole, every mip, layer and
  aspect, in the library's zero pass, now labeled `maul-rhi: zero new
  objects`, which takes nothing from the program's budgets. Before, a
  new texture held whatever its memory held on Vulkan, Metal and
  Direct3D 12, and zeros on WebGPU.
  - A texture of one mip and layer, not a volume, whose first use in
    the frame is a whole color target that clears and keeps it, or a
    depth target that clears and keeps depth and any stencil, with no
    other use in that pass, is not cleared, since that use writes all of
    it. Textures a frame declares keep their read-before-write rule and
    are not zeroed, nor are adopted images or textures made
    `mrhi_textureTransient`.
  - The clear adds usage: on Vulkan, `TRANSFER_DST` on every image the
    library makes but transient ones; on Direct3D 12,
    `ALLOW_DEPTH_STENCIL` on depth textures made without render target
    usage; on Metal, `MTLTextureUsageRenderTarget` on depth textures
    made without render target usage. Their cost on hardware GPUs is
    unmeasured.
  - Measured on 64 MiB textures, best of five: on WARP, 0.7 to 0.8 ms by
    a view clear and 5 to 7 ms by copies; on Metal's paravirtual device,
    23 to 38 ms, about what a 64 MiB texture's own first write costs
    there (25 to 32 ms); on lavapipe, 28 to 34 ms, of which the first
    write alone takes 15 to 21 ms. Direct3D 12's buffer of zeros grows
    from 64 KiB to 1 MiB.
  - Drivers built outside the tree must handle the new
    `mrhiCommandClearTexture` record and declare `MRHI_SPI_VERSION` 8; a
    driver of SPI version 7 is refused with `mrhi_errorVersion`. The
    mustpass list is `conformance/mustpass/spi-8.txt`. Shader cooks are
    not affected. `contract_version` rises to 17
    ([#137](https://github.com/Rawframe-Project/maul-rhi/pull/137)).

## [0.7.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.6.0...v0.7.0) (2026-10-26)

### Added

- Every new buffer reads as zeros until the frame graph writes it, on
  every driver. The first frame that imports a buffer clears it whole,
  in a transfer pass of the library's own that takes nothing from the
  frame's budgets, so the program's first use of the buffer sees zeros;
  passes that do not use it may run beside the clear. Before, a new
  buffer held zeros on WebGPU and Metal, usually zeros on D3D12, and on
  Vulkan possibly a destroyed buffer's old bytes. The cost is paid once
  per buffer, in its first frame; a frame with no new buffer runs
  without the pass. Measured clear times, best of five: on lavapipe 0.04
  ms for 1 MiB, 29.4 ms for 64 MiB and 458 ms for 1 GiB; on Metal's
  paravirtual device 1.67 ms, 28.4 ms and 196 ms; D3D12 below. A device
  with the default def takes 157,736 more bytes on lavapipe for the
  reserved pass, uses, command chunks and barriers. The program's pass
  ids now start at 2, and the frame log counts the library's pass, its
  clears and its accesses in a buffer's first frame. Drivers built
  outside the tree meet a first transfer pass of clears, see the copy
  destination usage on every buffer, and receive a device def whose
  framePasses and frameBarriers include the library's share; the SPI
  headers do not change. `contract_version` rises to 14, and to 15 for
  the wording of the first-use promise
  ([#132](https://github.com/Rawframe-Project/maul-rhi/pull/132),
  [#133](https://github.com/Rawframe-Project/maul-rhi/pull/133)).
- Metal runs async compute and transfer passes on command queues of
  their own, beside the graphics queue, ordered by one `MTLEvent` per
  lane, so they may overlap graphics work; a pass touching a surface
  image stays on the graphics queue. A native pass's command buffer must
  still come from the queue `mrhiGetMetalDevice` returns, now named the
  graphics queue. The overlap is unmeasured: CI's macOS runners have
  only Apple's paravirtual device. `contract_version` rises to 12
  ([#121](https://github.com/Rawframe-Project/maul-rhi/pull/121)).

### Changed

- **Breaking:** shader containers are version 2, and version 1 is
  refused with `mrhi_errorVersion`, so every container must be written
  again with this release's `tools/mrhi_container.py`. Sections are
  tagged fields: an unknown section type is `mrhi_errorVersion`, an
  empty section is refused, an absent meta is a root block of 0 bytes,
  and the sections must fill the container in order with zero padding,
  which gives a container one encoding and one digest. Containers are 24
  to 136 bytes smaller. A shader cook of its own must follow the layout
  and default rules. `contract_version` rises to 9
  ([#110](https://github.com/Rawframe-Project/maul-rhi/pull/110)).
- **Breaking:** a pipeline cache is read only under the ABI version that
  wrote it, `MRHI_ABI_VERSION`: before 1.0 every other release refuses
  it, a patch release included. A cache of another ABI or envelope
  version is refused with `mrhi_errorVersion` instead of
  `mrhi_errorStale`, which still names another driver or adapter; the
  device opens either way. Caches written by 0.6.0 or earlier are
  refused. Keep shader containers, not caches, across an upgrade.
  `contract_version` rises to 10
  ([#108](https://github.com/Rawframe-Project/maul-rhi/pull/108)).
- **Breaking:** every call that gives a pointer or an id sets each one
  it was given to NULL or the null id on any failure, before checking
  its owner: the creations, `mrhiRequestAdapters`, `mrhiCreateDevice`'s
  request, the declarations and imports, `mrhiAcquireSurfaceImage`,
  `mrhiAddPass`, `mrhiReadBuffer`, `mrhiReadTexture` and
  `mrhiSubmitFrame`. Before, most left their outputs as they were, and
  the two reads documented their request as untouched on failure.
  `contract_version` rises to 11
  ([#117](https://github.com/Rawframe-Project/maul-rhi/pull/117)).
- D3D12 clears a buffer, for `mrhiClearBuffer` and for the zeroing of
  new buffers, with `ClearUnorderedAccessViewUint` instead of copies
  from a 64 KiB buffer of zeros; past a frame slot's views it still
  copies. Every D3D12 buffer allows unordered access, and a transfer
  pass that clears runs on the compute queue, since copy lists cannot
  clear: it loses its overlap with async compute work. Transfer passes
  without clears stay on the copy queue. On WARP, zeroing a new buffer
  went from 615.6 to 189.8 us for 1 MiB and from 29.8 to 1.45 ms for 64
  MiB; a standalone program cleared 1 GiB in 21.6 ms instead of 496 ms.
  No hardware GPU was measured. `contract_version` rises to 16
  ([#134](https://github.com/Rawframe-Project/maul-rhi/pull/134)).

### Fixed

- The Vulkan mapping rows of async compute and transfer passes said a
  compute-only and a transfer-only queue family; they now say the
  graphics queue, in frame order, which is what the driver does. Only
  readers of the mapping tables see a change. `contract_version` rises
  to 13 ([#123](https://github.com/Rawframe-Project/maul-rhi/pull/123)).

## [0.6.0](https://github.com/Rawframe-Project/maul-rhi/compare/v0.5.0...v0.6.0) (2026-10-25)

### Added

- `mrhiWaitDeviceNotification(device, timeoutNs)` waits until the
  device's notification queue holds a record, or the deadline passes,
  without taking the record. It returns `mrhi_success` once a record is
  there, a lost device's notice included, and `mrhi_timeout` when the
  deadline passes first; a NULL device is `mrhi_errorInvalid`. When the
  queue is empty and the device owes no answer (no pipeline pending, no
  frame running) it returns `mrhi_empty` at once, so a program never
  sleeps for an answer that cannot come; a readback is owed only once
  its frame is submitted. The timeout is in nanoseconds, as
  `mrhiWaitFrame`'s, and 0 only checks. On native drivers a pending
  pipeline's answer is already there at the first poll, and otherwise it
  waits for the oldest running frame through the driver's frame wait, so
  a program waiting for a pipeline, a frame or a readback no longer
  spins. On the web it never blocks: it finds only an answer the browser
  has already settled, and otherwise times out at once.
  `contract_version` rises to 8; drivers built outside the tree see no
  change, since the SPI is untouched
  ([#102](https://github.com/Rawframe-Project/maul-rhi/pull/102)).

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
