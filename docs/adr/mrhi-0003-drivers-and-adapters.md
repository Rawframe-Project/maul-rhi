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
- **The WebGPU driver:** built with `MAUL_RHI_WEBGPU_DRIVER`, on in
  every web build, and started by an instance with no test driver
  chained. It calls the browser's WebGPU through `EM_JS` functions
  compiled into its objects, each instance keeping its GPU objects and
  its waiting requests on the JavaScript side; a promise settles into a
  queue the driver's poll drains, so nothing blocks, and a program
  returns to the page's event loop to see answers. A search lists the
  browser's adapter under one handle, so it keeps its id; one without
  WebGPU's core features, or without immediates, which carry the root
  block, falls below the floor. Its features and limits follow the
  contract's WebGPU rows, and its format capabilities are the floor and
  what its features add.
- **WebGPU devices:** a device asks a fresh adapter for its GPUDevice,
  since a WebGPU adapter makes one device, with the granted features
  and limits (a limit below WebGPU's default is raised by the browser);
  the opening is answered when the browser settles it. Objects live on
  the JavaScript side under small handles; a destroyed one waits there
  until the next frame submitted after its destruction finishes, since
  frames are recorded at submission. A view of one aspect takes the
  format the browser resolves for it. Loss is reported by the next
  poll with the browser's message. WebGPU places memory itself, so a
  declared resource's bytes are an estimate for the frame's report.
  Each device holds a validation error scope for its life, read as it
  closes, so that the web test runner fails on any WebGPU error.
- **WebGPU pipelines:** a shader is a module over the container's WGSL.
  A pipeline's layout is the whole container's: a bind group layout per
  table up to the last one used, empty for a table without bindings,
  and the root block's bytes as immediates. Pipelines are made with the
  browser's asynchronous calls and answered by the poll after they
  settle; one refused is reported as a platform failure and kept with
  the device's errors, since the core has checked it. The driver's
  pipeline cache is empty, and it takes back only its own.
- **WebGPU frames:** a frame is recorded at submission into one command
  encoder, one call into JavaScript per command, and the browser's
  synchronization replaces the core's barriers. A pass with targets is
  a render pass; a graphics pass without them is a compute pass, which
  leaves its GPU compute pass for copies, query resolves and debug
  labels on the command encoder and sets its pipeline, bind groups and
  immediates again in the next one, its end timestamp then written by
  a pass of its own. A binding table is a bind group made at its
  command in the pipeline's layout. Uploads are written into one
  staging buffer before the frame's commands, so queue order keeps
  them apart from earlier frames'; readbacks are copied into a mirror
  of the ring, mapped after submission and copied into the ring before
  the frame is reported finished. A frame without readbacks finishes
  when the queue has done its work. Frames are reported in order.
  Declared resources come from a pool keyed by their descriptors,
  each its own object, never aliased, and one no frame took in the
  last 8 is destroyed. A query set keeps its values across frames, so
  the queries a frame did not write are cleared to 0 after a resolve.
- **WebGPU canvases:** a surface is the canvas its selector names, with
  its WebGPU context; a selector naming no canvas, or a canvas without
  a WebGPU context, is a source the driver cannot use. Its colors are
  the browser's preferred 8-bit format first, then the other, with the
  sRGB curve, and half floats in linear values, each in Rec. 709 and
  Display P3, the floats also in extended range through extended tone
  mapping; it presents in order only, opaque or premultiplied.
  Configuring sets the canvas's drawing buffer to the configured size,
  so the size is the program's own; an acquired image is the context's
  current texture, out of date once the page resizes the drawing
  buffer, and the browser presents it when the page returns to its
  event loop, so an image given back is only forgotten.
- **The Metal driver:** built with `MAUL_RHI_METAL_DRIVER` on Apple
  systems, off by default until it runs everything the core records,
  and never beside the Vulkan driver: a build has one native driver.
  It speaks Metal's classic API (command queues, command buffers and
  encoders), which every Metal device runs, the virtual one of hosted
  macOS runners included; a path through Metal 4's argument tables may
  come inside the driver where a device reports that family. Its files
  are Objective-C (`src/*.m`) in the family's C23 dialect, compiled
  with manual retain and release so that Metal objects sit in plain C
  structs, released where the SPI destroys them; every entry point that
  touches Objective-C objects drains its own autorelease pool. A search
  lists every Metal device (the default one outside macOS) under its
  registry id. Limits are WebGPU's floor, raised where Metal's feature
  set tables promise more for the device's family and where the device
  reports its own; features are granted as the driver comes to run
  them. MoltenVK and KosmicKrisp stay usable under the Vulkan driver
  where a program builds it and loads one, but are neither tested nor
  promised. CI runs the conformance suite on the macOS runner's device
  under `MTL_DEBUG_LAYER` and `MTL_SHADER_VALIDATION`; until the
  driver runs frames, it checks objects and pipelines only.
- **Metal objects:** buffers, textures, views, samplers and query sets
  are Metal objects whose retained pointers are their handles, released
  as soon as the core destroys them, since command buffers retain what
  they use. Buffers and textures live in private memory, as the frame
  graph fills and reads them; a transient render target is memoryless
  on Apple family GPUs. The depth and stencil format is
  Depth32Float_Stencil8, whose stencil a view sees as X32_Stencil8. An
  occlusion query set is a buffer of 8 bytes per query, the render
  passes' visibility results. Samplers are made to sit in argument
  buffers. A resource's memory is Metal's heap size and alignment for
  it.
- **Metal pipelines:** a shader holds a library per entry point, made
  from the container's metallib when it has one and otherwise compiled
  from each entry's MSL without fast math, and a copy of the Metal map.
  Each entry is the function of its own name, specialized with the
  pipeline's constants as function constants of their ids. Pipelines
  are made at the call and answered at the next poll, and keep what
  their frames bind, so they outlive their shader. Vertex attributes
  take their locations as attribute indices, and vertex buffer n lies
  at buffer index 30 minus n; a pipeline whose vertex buffers reach an
  index the container uses is refused as unsupported, as is a sample
  mask that leaves samples out, since Metal has none, and a workgroup
  larger than the pipeline's threads. A buffer of stride 0 takes
  Metal's constant step. The driver's pipeline cache is empty, since
  Metal keeps its own.
- **Metal frames:** a frame is recorded at submission into one command
  buffer: a render encoder for a pass with targets, a compute encoder
  for one without, a blit encoder for a transfer pass; copies and
  resolves in a compute pass leave it for a blit encoder and the next
  compute encoder takes up the pipeline and state again, the pass's
  debug groups closed and reopened per encoder. Bindings, the root
  block and SPIRV-Cross's buffer sizes are applied through the
  pipeline's map before each draw or dispatch. Metal tracks hazards
  between encoders, so the core's barriers need no commands. Uploads
  are copied into the slot's shared staging buffer and readbacks land
  in a shared mirror of the ring, copied into it once the frame
  finishes; declared resources are made per frame and released once
  the command buffer, which holds them, is committed. The query sets a
  frame resolves are cleared first, so a query it does not write reads
  0. A poll reads each command buffer's status in order; a waiting
  program waits on a semaphore the completion signals; a failed command
  buffer is the device's loss. A destroyed object or pipeline is
  released after the next frame's commit.
- **Metal surfaces:** a surface is a CAMetalLayer, retained, and every
  adapter presents to it: 8-bit sRGB in Rec. 709 and Display P3, and
  half floats, linear, of standard or extended range, in both; fifo
  presentation, and immediate on macOS; opaque or premultiplied alpha.
  Configuring sets the layer's device, pixel format, color space,
  extended range, drawable size, synchronization and opacity; a
  program's size is its own, since the layer scales drawables to its
  bounds. An acquire takes the next drawable: out of date when
  something else resized the drawables, suboptimal while the layer's
  bounds in pixels differ. A frame presents each image it acquired
  after its work.
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
  reports them in order. A frame also waits on the timeline for the
  frame that used its slot before it: the host has seen that frame
  finish before reusing the slot's memory, so the wait costs nothing,
  but it puts the reuse in the queue's order, where synchronization
  validation sees it. A destroyed object waits in a queue and is
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
- **Vulkan labels:** the instance enables `VK_EXT_debug_utils` where
  the loader offers it, which costs nothing unless a tool listens.
  Devices, buffers, textures, views, samplers, query sets, shaders and
  pipelines are named from their defs' labels; each labelled pass is a
  labelled region around its barriers and work; debug groups and
  markers become command labels. Labels are copied with a NUL, as
  Vulkan reads them. Without the extension, labels are dropped.
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
  CTest fails any test whose output reports a validation error. Lavapipe
  poisons new memory there (`LVP_POISON_MEMORY`), so reading memory
  nothing wrote fails reliably. The samples run as tests the same way.
  Where the XCB client library is installed, the suite makes a window
  and checks surfaces on it; Linux CI runs the tests under Xvfb and
  sets `MAUL_RHI_REQUIRE_SURFACE`, so a missing X server fails. A second
  container (`test/shaders/bindless.*`, without WGSL) reads heaps on
  adapters with heterogeneous heaps; Linux CI sets
  `MAUL_RHI_REQUIRE_BINDLESS`, so lavapipe must grant them.
  On the web the suite runs in headless Chrome with WebGPU
  (`test/web_runner.cjs`, Puppeteer from `MRHI_NODE_MODULES`) on
  Chrome's Vulkan path over the SwiftShader it ships, named as the only
  Vulkan driver, since its default SwiftShader path destroys a device
  that presents to a canvas. It is built with
  JSPI so that it sleeps while the browser settles its promises; an
  error the browser logs fails it, and CI sets
  `MAUL_RHI_REQUIRE_WEBGPU`.
  ThreadSanitizer skips the threads and locks of lavapipe, LLVM and
  the XCB client library (`test/tsan.supp`), which it cannot see
  ordered.

## Consequences

One request shape serves the browser and native drivers. Every refusal
and ordering rule of the core is tested in CI on every platform, with
no GPU.
