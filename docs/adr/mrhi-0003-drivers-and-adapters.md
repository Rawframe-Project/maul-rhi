# mrhi-0003. Drivers behind an SPI, adapters by request, and a test driver

Status: Accepted

## Context

The library speaks to Vulkan, WebGPU, Metal and Direct3D 12, and later
to closed console drivers. WebGPU finds one adapter per promise, where
native APIs list every adapter at once. Much of the core (validation,
ids, requests, the frame graph) can be tested without a GPU.

## Decision

- **The SPI:** a driver is an instance vtable and a device vtable
  beside its own pointer. Each vtable opens with the SPI version and
  its size, which the core checks. Handles crossing the SPI are 64-bit,
  zero invalid, and the core maps its ids onto them. A driver reports
  finished work only when the core polls it, and never calls the core.
- **Compiled in:** drivers are compiled into the library, since the web
  loads no code at run time. An out-of-tree driver is handed to an
  instance through a critical chained struct.
- **Adapters by request:** `mrhiRequestAdapters` takes a preference
  and is answered by one record in the instance's notification queue,
  which draining polls the drivers for. `mrhiGetAdapters` then lists
  the adapter ids, best first. An adapter found again keeps its id; one
  gone is stale, and a slot reused takes a new generation.
- **The test driver:** built with `MAUL_RHI_TEST_DRIVER` and turned on
  by `mrhiTestDriverDef` on an instance def. It has no GPU, finds the
  adapters the test describes and answers at the next poll.
- **The Vulkan driver:** built with `MAUL_RHI_VULKAN_DRIVER`, on in
  every native build, and started by an instance with no test driver
  chained. It compiles against the Khronos C headers, kept exactly as
  published in `khronos/`, and opens the loader when the instance
  starts (`libvulkan.so.1`, `vulkan-1.dll`, `libvulkan.1.dylib`),
  reading every function through `vkGetInstanceProcAddr`; with no
  loader, or no Vulkan 1.3, the instance has no driver. It lists a
  physical device that reports Vulkan 1.3, a queue family with graphics
  and compute, dynamic rendering, synchronization2, timeline
  semaphores, buffer device addresses and descriptor indexing, and the
  Vulkan 1.0 features the floor uses (`fullDrawIndexUint32`,
  `imageCubeArray`, `independentBlend`, `sampleRateShading`,
  `depthBiasClamp`, `fragmentStoresAndAtomics`, `samplerAnisotropy`,
  `shaderStorageImageExtendedFormats`), keyed by the physical device,
  so a device found again keeps its id.
- **Conformance:** `test_conformance` runs the same checks through the
  public API on the test driver and on every native adapter. A host
  without one skips it unless `MAUL_RHI_REQUIRE_VULKAN` is set; Linux
  CI sets it, runs lavapipe, and enables the Khronos validation layer,
  which logs to each test's output; CTest fails any test whose output
  reports a validation error.

## Consequences

One request shape serves the browser and native drivers. Every refusal
and ordering rule of the core is tested in CI on every platform, with
no GPU.
