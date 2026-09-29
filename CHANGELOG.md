# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Added

- The library skeleton: the build, the family rules and tools, the
  version and result API (`mrhiGetVersion`, `mrhiResultName`) and the
  library profile.
- The contract (`docs/contract/mrhi.json`), the source of truth for
  the public headers and the thread safety table, and its generator
  (`tools/gen_contract.py`), checked for drift in CI (mrhi-0002).
- Instances (`mrhiCreateInstance`, `mrhiDestroyInstance`,
  `mrhiDefaultInstanceDef`): the contract version checked exactly
  (`MRHI_CONTRACT_VERSION`, `mrhi_errorVersion`), the caller's
  allocator, and defs that open with a cookie and an extension chain
  whose unknown critical structs are refused and whose hints are
  skipped, to a named depth.
- The family's result codes (`mrhi_errorStale`,
  `mrhi_errorUnsupported`, `mrhi_errorPlatform`, `mrhi_errorState`),
  with their names generated from the contract.
- Adapters (mrhi-0003): `mrhiRequestAdapters` with a power preference,
  answered once in the instance's notification queue
  (`mrhiNextInstanceNotification`, `mrhi_empty`), then
  `mrhiGetAdapters` and `mrhiGetAdapterInfo` by generation-checked id;
  the instance's notification and adapter limits.
- The test driver (`MAUL_RHI_TEST_DRIVER`, `mrhiTestDriverDef`): no
  GPU, the adapters a test describes.
- Capabilities (`capabilities.h`): the optional features
  (`mrhiFeatures`) and the limits (`mrhiLimits`, `mrhiDefaultLimits`,
  WebGPU's floor), read per adapter (`mrhiGetAdapterFeatures`,
  `mrhiGetAdapterLimits`). Each maps onto Vulkan, D3D12, Metal and
  WebGPU in `docs/contract/mappings.md`; adapters below the floor are
  left out, and features an API cannot grant are never reported.
- The instance's misuse count (`mrhiGetInstanceMisuse`): each call
  refused as invalid input on a live instance counts once.
- Devices (mrhi-0004, `device.h`): `mrhiCreateDevice` on an adapter
  with the features and limits asked for, returning at once and ready
  after `mrhi_instanceDeviceReady`; `mrhiDestroyDevice`,
  `mrhiGetDeviceState`, the granted `mrhiGetDeviceFeatures` and
  `mrhiGetDeviceLimits`, and `mrhiGetDeviceMisuse`. The test driver
  opens devices with the outcome its adapter describes.
- Formats (`mrhiFormat`): the requirements' color formats, 32-bit
  integers for atomics, `depth32Float`, an abstract `depthStencil`, and
  the BC, ETC2 and ASTC families behind their features, each mapped
  onto the four APIs; `mrhiGetFormatCaps` per adapter, with WebGPU's
  guaranteed capabilities as the floor every listed adapter meets.
- Samplers (`resources.h`): `mrhiCreateSampler` and
  `mrhiDestroySampler` with WebGPU's defaults (`mrhiDefaultSamplerDef`),
  no border colors, anisotropy only with linear filtering; filters,
  address modes and comparisons mapped onto the four APIs. Every device
  object is a generation-checked id over a table sized by the device's
  limits (`samplers`), and a destroyed one's id ends at once.
- The test driver fails a device's objects after a set number
  (`objectsBeforeFailure`), to test platform failures.
- Buffers: `mrhiCreateBuffer` and `mrhiDestroyBuffer` with declared
  usages (`mrhiBufferUsage`, mapped onto the four APIs), sizes that are
  multiples of 4 up to the device's `bufferBytes`, never mapped; the
  device's `buffers` limit.
- Textures: `mrhiCreateTexture` and `mrhiDestroyTexture` with a kind
  (`mrhiTextureKind`: 2D, 2D array, cube, cube array, 3D), mips up to a
  full chain, sample counts the format allows, declared usages
  (`mrhiTextureUsage`, transient render targets included) checked
  against the format's capabilities on the device, and the sRGB or
  linear twin its views may take (`viewFormats`); sizes checked against
  the kind, the format's block and the device's limits; the device's
  `textures` limit.
- Views: `mrhiCreateView` and `mrhiDestroyView` over a texture's mips
  and layers (`MRHI_REMAINING` for the rest), with a kind the texture
  allows, its format or a view format it was given, some of its usages
  (0 for all) that the view's format can take on the device, and an
  aspect (`mrhiTextureAspect`) its format has; each mapped onto the four
  APIs. Destroying a texture ends its views; the device's `views` limit.
- Debug labels: every device, sampler, buffer, texture and view def
  takes a `label` and `labelLength`, well-formed UTF-8 without NUL of at
  most `MRHI_LABEL_BYTES`, handed to the driver during the call and
  never kept; each mapped onto the four APIs' object names.
- Surfaces (mrhi-0007, `surface.h`): `mrhiCreateSurface` from exactly
  one chained native source (Win32, Wayland, XCB, Android, a Metal
  layer, a web canvas), `mrhiDestroySurface`, and `mrhiGetSurfaceCaps`
  per adapter: whether it presents there, its color combinations
  (`mrhiSurfaceColor`: format, primaries, transfer, range), present
  modes, alpha modes and usages, with `fifo`, opaque alpha and render
  targets as the floors; each mapped onto the four APIs. Adapter
  searches may name a `compatibleSurface`; the instance's `surfaces`
  limit; the test driver's `mrhiSurfaceSourceTest`.
- Surface configuration: `mrhiConfigureSurface` (`mrhiSurfaceConfig`:
  a reported color, the twin as a view format, usages, a size, one
  present mode and one alpha mode, each mapped onto the four APIs) and
  `mrhiUnconfigureSurface`; one device at a time, reconfiguring in
  place, and configurations ended with their surface or device; the
  device's `surfaces` limit.
- Frames (mrhi-0008, `frame.h`): `mrhiBeginFrame`, `mrhiDropFrame` and
  `mrhiSubmitFrame`, one frame open at a time; the token each
  submission returns is answered once by a `mrhi_deviceFrameDone`
  record in the device's queue (`mrhiNextDeviceNotification`), and
  `mrhiWaitFrame` waits with a deadline (`mrhi_timeout`); the limit's
  `framesInFlight` refuses a new frame until one finishes. The test
  adapter can hold frames (`holdFrames`) and fail them
  (`frameOutcome`).
- Frame resources: `mrhiDeclareTexture` and `mrhiDeclareBuffer` for
  what the graph makes, checked as their device objects are but with
  usages left to the passes, and `mrhiImportTexture` and
  `mrhiImportBuffer` for device objects, one id per frame; frame-local
  `mrhiResourceId`s that end with their frame; the device's
  `frameResources` limit.
- Passes and the compile: `mrhiAddPass` (`mrhiPassDef`: an execution
  class, accesses of fixed kinds over texture ranges, color targets
  with load, store, clear and resolve, a depth target, never-cull),
  checked against each resource, the pass's class and WebGPU's usage
  scopes, with declared resources written before they are read;
  `mrhiCompileFrame` culls from the frame's outputs and checks declared
  textures' derived usages, `mrhiIsPassKept` reports what was kept, and
  submitting compiles a frame not yet compiled; classes, access kinds
  and operations mapped onto the four APIs; the device's `framePasses`
  and `frameAccesses` limits.
- The compile's plan: barriers per mip, layer and plane from the states
  uses leave (`mrhiResourceState`, mapped onto each API's layouts and
  accesses), readable with `mrhiGetFrameBarriers` in the order they run;
  imported objects carry their state between frames, unified at a
  frame's end; `mrhiGetResourcePlan` reads each resource's derived
  usages, transience (targets that live only inside their passes) and
  first and last kept passes; a texture's part is in one state per pass;
  the device's `frameBarriers` limit.
- Transient memory and stores: declared resources placed by lifetime in
  one frame memory, first fit in first-use order with the driver's sizes
  and alignments (`memoryOffset` and `memoryBytes` in the resource plan,
  `mrhiGetFrameMemory`), transient textures taking none where the GPU
  keeps them on chip; each target's store kept only when a later kept
  pass reads it or it is imported (`mrhiGetPassPlan`).
- Shader containers (mrhi-0009, `shader.h`, `docs/contract/container.md`):
  SPIR-V and WGSL beside one reflection in WebGPU's binding terms, with
  every fact pipeline validation reads (interface variables with
  `mrhiScalarType`, `mrhiInterpolation` and `mrhiSampling`,
  `mrhiShaderBuiltins`, workgroup storage, required constants),
  identified by a SHA-256 digest; `mrhiCreateShader` checks every byte
  as hostile input and the container against the device's limits;
  `mrhiDestroyShader` and `mrhiGetShaderInfo`; the device's `shaders`
  limit. `tools/mrhi_container.py` writes them from
  the two modules and a JSON reflection, refusing code that disagrees
  with it.
- Compute pipelines (mrhi-0010, `pipeline.h`): `mrhiCreateComputePipeline`
  returns the pipeline and a request answered by
  `mrhi_devicePipelineReady` in the device's queue, whose room it
  reserves; `mrhiDestroyComputePipeline` answers a pending one stale;
  specialization constants as doubles checked against their types,
  with every constant without a default required; pipelines keep working
  after their shader is destroyed; frames and pipelines share one
  request space; the device's `pipelines` limit and the test adapter's
  `pipelineOutcome`.
- Graphics pipelines: `mrhiCreateGraphicsPipeline` and
  `mrhiDestroyGraphicsPipeline` with every state of WebGPU's render
  pipeline descriptor (vertex buffers and attributes, WebGPU's vertex
  formats but for `snorm10-10-10-2`, primitive, depth and stencil,
  multisample, up to eight color targets with blending and write masks,
  constants), each mapped onto the four APIs and checked against the
  shader's reflection and the device as WebGPU checks it: invalid input
  for contradictions, unsupported for what the device cannot do. Color
  formats and vertex formats carry their render target and layout facts
  in the contract.
- Pipeline caches: `mrhiGetPipelineCache` writes the driver's blob in a
  checked envelope (SHA-256, library version, driver and adapter), and a
  device def's `pipelineCache` gives it back; an unusable cache never
  fails a device, and `mrhiGetPipelineCacheOutcome` reports whether it
  was taken, absent, stale or damaged.
- Encoders (mrhi-0011, `encoder.h`): `mrhiBeginPass` and `mrhiEndPass`
  claim and end a kept pass of the compiled frame, one thread per pass;
  `mrhiSetGraphicsPipeline`, `mrhiSetComputePipeline`,
  `mrhiSetRootBlock`, `mrhiSetViewport`, `mrhiSetScissor`,
  `mrhiSetBlendConstant`, `mrhiSetStencilReference` and the debug
  groups and markers, each checked against its pass as WebGPU checks it
  and recorded into the frame's arena, the device's `frameCommandBytes`
  limit. A full arena refuses the command and the frame's submission.
- Binding tables: `mrhiSetBindings` sets a whole table (`mrhiBinding`:
  a buffer range with `MRHI_WHOLE_SIZE`, a texture view, or a sampler),
  checked against the slots of the pass's pipeline as WebGPU checks a
  bind group, and against the pass's declared accesses. Shaders whose
  tables hold more than `MRHI_TABLE_BINDINGS` bindings are unsupported.
- Draws and dispatches: `mrhiSetVertexBuffer`, `mrhiSetIndexBuffer`,
  `mrhiDraw`, `mrhiDrawIndexed` and `mrhiDispatch`, refused unless the
  pass's pipeline, the tables it reads and vertex and index buffers
  large enough for the elements drawn are set, as WebGPU refuses them.
- Indirect draws and dispatches: `mrhiDrawIndirect`,
  `mrhiDrawIndexedIndirect` and `mrhiDispatchIndirect`, their arguments
  in a buffer the pass declares with the indirect access, checked as
  WebGPU checks them; the arguments themselves are read on the GPU.
- Copies: `mrhiCopyBuffer`, `mrhiCopyBufferToTexture`,
  `mrhiCopyTextureToBuffer` and `mrhiCopyTexture` (`mrhiBufferCopy`,
  `mrhiTextureCopy`, `mrhiExtent3d`), checked as WebGPU checks them and
  against the pass's copy accesses; formats carry their texel copy
  footprint and copy directions per aspect in the contract.
- Uploads: `mrhiWriteBuffer` and `mrhiWriteTexture` (`mrhiTexelLayout`)
  copy the program's bytes at the call into the frame's staging, the
  device's `frameUploadBytes` limit per frame in flight, checked as
  WebGPU checks its queue writes; a full staging refuses the frame.
- Readbacks: `mrhiReadBuffer` and `mrhiReadTexture` copy into the
  device's readback ring (the `readbackBytes` and `readbacks` limits)
  and return a request that the frame's finish answers with
  `mrhi_deviceReadbackReady`; `mrhiTakeReadback` copies the bytes out
  once, a texture's rows tightly packed, and frees them in order. A
  dropped or failed frame gives its ring room back.
- Query sets (mrhi-0012): `mrhiCreateQuerySet`, `mrhiDestroyQuerySet`
  and `mrhiDefaultQuerySetDef` (`mrhiQuerySetDef`, `mrhiQueryType`),
  occlusion or timestamp sets of up to 4096 queries, timestamps with
  the `timestamp_query` feature, within the device's `querySets` and
  `queries` limits.
- Occlusion queries: a render pass names an occlusion query set
  (`occlusionQuerySet`), and `mrhiBeginOcclusionQuery` and
  `mrhiEndOcclusionQuery` bracket its draws, one query open at a time
  and each query written at most once a frame.
- Timestamps: a graphics pass names a timestamp query set
  (`timestampQuerySet`) and the queries written at its start and end
  (`timestampBegin`, `timestampEnd`, `MRHI_NO_QUERY` for none), each
  written at most once a frame; `mrhiGetDeviceTimestampPeriod` reads
  the nanoseconds per tick.
- Resolving queries: `mrhiResolveQueries` writes a set's queries as
  64-bit values into a buffer a graphics pass without targets declares
  with the new query resolve access (`mrhi_accessQueryResolve`, leaving
  `mrhi_stateQueryResolve`), at a 256-byte boundary.
- Command streams name pipelines, samplers and query sets by their
  driver handles, so an object destroyed and its slot reused while a
  frame records never changes what the frame's streams name.
- Clang builds for 64-bit targets warn when a 64-bit value is cut to
  32 bits (`-Wshorten-64-to-32`), and the test driver's handles start
  above 32 bits.
- Submission (mrhi-0013): a submitted frame reaches its driver as one
  read-only view of its resources, kept passes, barriers, commands,
  uploads and readback ring. Imported objects are snapshotted into the
  frame, so one destroyed while the frame is open never affects the
  object that takes its slot. The test driver walks every submitted
  frame's commands and reports them in `mrhiTestFrameLog`
  (`mrhiTestAdapter.frameLog`).
- Surface images (mrhi-0013): `mrhiAcquireSurfaceImage` brings a
  configured surface's next image into the open frame as a texture that
  ends in the new `mrhi_statePresent` and is presented at submission;
  new results `mrhi_suboptimal`, `mrhi_occluded`, `mrhi_errorOutOfDate`
  and `mrhi_errorDeviceLost`; the test adapter's `acquireOutcome` picks
  what acquiring answers.
- Device loss (mrhi-0014): a lost device answers a
  `mrhi_deviceLostNotice` and everything it owed with
  `mrhi_errorDeviceLost`, refuses GPU work with that code, and gives a
  fixed-size `mrhiDeviceLossReport` (`mrhiGetDeviceLossReport`); one
  notification record is kept for the notice, so devices hold at least
  2; the test adapter's `loseDevice` and `lossReason` inject a loss.
- Mapping appendix (mrhi-0002): every function, struct, enum and
  bitflags is mapped onto the four APIs or declared the library's own
  with the reason, and emulated rows state their cost; the generator
  refuses a concept classed no way or more than one. D3D12 and Metal
  read DXIL and metallib sections made offline, so the library never
  translates shaders at run time.
- The Vulkan driver begins (mrhi-0003, `MAUL_RHI_VULKAN_DRIVER`, on in
  native builds): the loader opened at run time, a Vulkan 1.3
  instance, and physical devices that meet the driver's floor listed as
  adapters with their features, limits and format capabilities; lavapipe
  is a software adapter. The Khronos C headers are kept as published in
  `khronos/`. Devices open with the floor's features and the granted
  ones, one queue and a timeline semaphore; buffers, textures, views
  and samplers are made in device-local memory suballocated with TLSF;
  shaders and compute and graphics pipelines are made, answered at the
  next poll, with a pipeline cache. Frames of copies, uploads and
  readbacks run, with transients in each frame slot's memory, and
  destroyed objects retire after the next frame finishes. Passes draw
  and dispatch, with +Y up, and query sets measure occlusion and
  timestamps. Surfaces are made from XCB, Wayland, Win32, Android and
  Metal layer sources and report their colors, present modes, alpha
  modes and usages. Configured surfaces get swapchains whose images
  frames acquire, render to and present; an image a dropped frame gave
  back is handed out again. A configuration whose size the window does
  not take is `mrhi_errorOutOfDate`, and one naming the twin view
  format where the device cannot give it is unsupported. Labels name
  objects and mark passes, debug groups and markers through
  `VK_EXT_debug_utils` where the loader offers it.
- A table's bindings record their count as the command's payload, so a
  driver steps over them; before, a driver read the bindings as
  commands.
- The conformance suite (`test_conformance`): the same checks through
  the public API on the test driver and every native adapter, run on
  lavapipe under the Khronos validation layer in Linux CI.
