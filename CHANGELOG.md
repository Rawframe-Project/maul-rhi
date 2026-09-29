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
