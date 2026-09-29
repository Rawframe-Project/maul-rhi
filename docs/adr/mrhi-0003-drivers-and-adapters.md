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
- **Vulkan memory:** buffers and textures are suballocated with TLSF
  (`docs/references.md`) from device-local blocks per memory type and
  kind, buffers apart from textures so that `bufferImageGranularity`
  never applies; a block is 64 MiB or an eighth of its heap if smaller,
  and a resource the driver wants alone or over half a block gets its
  own allocation. An empty block is freed unless it is its pool's last.
  The bookkeeping is sized from the device limits when the device is
  made. Each frame slot will keep one block for its transients, and
  mapped staging and readback memory come with the device.
- **Vulkan pipelines:** a shader is a module over the container's
  SPIR-V. A pipeline's layout comes from the reflection: a descriptor
  set per binding table up to the last one used, each slot a binding,
  and the root block as one push constant range for the pipeline's
  stages. Graphics pipelines use dynamic rendering, with the viewport,
  scissor, blend constant and stencil reference dynamic. Pipelines are
  made at the call, with no thread, and answered at the next poll; the
  device keeps one pipeline cache, which takes a blob only when
  Vulkan's own header names the device and its cache UUID.
- **Vulkan frames:** each frame in flight has a slot: its command
  buffer, its transients made at submission and bound where the core
  placed them in the slot's block (grown when a frame needs more), its
  mapped staging, and the readback ranges it fills. A frame signals the
  device's timeline semaphore with its serial, so a poll reads one
  counter, copies finished frames' readbacks into the core's ring and
  reports them in order. A destroyed object waits in a queue and is
  destroyed once the next frame submitted after its destruction
  finishes; the driver's tables hold twice the objects the core allows,
  so a program may replace each object once between submissions. The
  core's resource states map to Vulkan stages, accesses and layouts for
  its barriers.
- **Vulkan passes:** a pass renders between `vkCmdBeginRendering` and
  `vkCmdEndRendering` over views of its targets, a resolve averaging
  samples; its viewport covers the targets with a negative height, so
  that +Y points up in normalized device coordinates as on every
  driver, with the targets' origin at their top-left corner, and every
  draw state starts at its default. A table's bindings are written into a
  descriptor set taken from the slot's pools, which reset when the slot
  is reused, and each recorded binding carries its slot's kind so the
  driver knows the descriptor type without the reflection. The root
  block is pushed as constants to the pipeline's stages.
- **Vulkan surfaces:** the platform headers of the same Khronos tag
  are kept beside `vulkan_core.h` and compiled on their platform only
  (XCB and Wayland on Linux and the BSDs, Win32, Android, Metal); the
  driver declares the three XCB types the XCB header names, so no
  window library is needed to build. The instance enables
  `VK_KHR_surface`, the platform surface extensions and
  `VK_EXT_swapchain_colorspace` its loader offers; a source without
  its extension, and a canvas, are unsupported. An adapter presents to
  a surface when it offers `VK_KHR_swapchain` and its queue family
  presents there; its colors are the surface's format and color space
  pairs the contract's rows map, an sRGB format standing for its unorm
  twin. A configuration makes a swapchain of at least three images,
  with `VK_KHR_swapchain_mutable_format` for the twin; a retired
  swapchain is destroyed once the queue is idle. Each swapchain has a
  semaphore per image, signalled by the frame for its present, and one
  acquire semaphore more than its images, each reused once the frame
  that waited on it finishes. A frame waits on its images' acquires,
  which its first transitions wait for, and its images are presented
  after its submission; a present's result is reported at the next
  acquire, and a window with no area is occluded. An image a frame
  gives back stays acquired and is handed out at the next acquire.
- **Vulkan queries:** a query set is a query pool. Each set a frame
  names is reset once before its first pass; a pass's timestamps are
  written before and after it, and occlusion queries bracket draws. As
  it records, the driver marks the queries the frame writes, and a
  resolve copies those with their results awaited and fills the others
  with 0, since Vulkan writes nothing for a query never written
  (mrhi-0012).
- **Conformance:** `test_conformance` runs the same checks through the
  public API on the test driver and on every native adapter, with a
  shader container made offline from `test/shaders/` by
  `tools/gen_test_shaders.py` (glslang, spirv-link, spirv-val and the
  container writer), committed as bytes so CI needs no shader tools. A host
  without one skips it unless `MAUL_RHI_REQUIRE_VULKAN` is set; Linux
  CI sets it, runs lavapipe, and enables the Khronos validation layer
  with synchronization validation, which logs to each test's output;
  CTest fails any test whose output reports a validation error.
  Where the XCB client library is installed, the suite makes a window
  and checks surfaces on it; Linux CI runs the tests under Xvfb and
  sets `MAUL_RHI_REQUIRE_SURFACE`, so a missing X server fails.
  ThreadSanitizer skips the threads and locks of lavapipe, LLVM and
  the XCB client library (`test/tsan.supp`), which it cannot see
  ordered.

## Consequences

One request shape serves the browser and native drivers. Every refusal
and ordering rule of the core is tested in CI on every platform, with
no GPU.
