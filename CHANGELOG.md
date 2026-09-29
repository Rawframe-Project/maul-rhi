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
