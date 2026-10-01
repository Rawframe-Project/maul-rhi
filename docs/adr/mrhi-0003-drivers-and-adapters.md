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
  every native build but Apple's, and started by an instance with no test driver
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
- **The Metal driver:** built with `MAUL_RHI_METAL_DRIVER`, on in
  every Apple build, where the Vulkan driver is off, since a build has
  one native driver; the other builds have no Metal driver.
  It speaks Metal's classic API (command queues, command buffers and
  encoders), which every Metal device runs, the virtual one of hosted
  macOS runners included; a path through Metal 4's argument tables may
  come inside the driver where a device reports that family. Its files
  are Objective-C (`src/*.m`) in the family's C23 dialect, compiled
  with manual retain and release so that Metal objects sit in plain C
  structs, released where the SPI destroys them; every entry point that
  touches Objective-C objects drains its own autorelease pool. A search
  lists every Metal device (the default one outside macOS) that meets
  the contract's floor, under its registry id: cube array textures,
  which Apple's GPUs have from the Apple4 family (A11) and every Mac's
  have, as the Vulkan driver asks `imageCubeArray`. Limits are WebGPU's floor, raised where Metal's feature
  set tables promise more for the device's family and where the device
  reports its own; features are granted as the driver comes to run
  them. MoltenVK and KosmicKrisp stay usable under the Vulkan driver
  where a program builds it and loads one, but are neither tested nor
  promised. CI runs the conformance suite and the samples on the macOS
  runner's device under `MTL_DEBUG_LAYER` and `MTL_SHADER_VALIDATION`.
  The same driver builds for iOS (15 and later): what iOS has later is
  used where it has it (BC compression from 16.4, extended range
  surfaces from 16), and presenting immediately is macOS's alone.
  `cmake/ios-simulator.cmake` builds for the iOS simulator and runs
  every test there through `simctl spawn`, which CI does; the
  simulator's GPU has no cube arrays, so the conformance suite's native
  part skips there, and the driver's own runs are the macOS runner's.
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
- **The D3D12 driver:** built with `MAUL_RHI_D3D12_DRIVER`, the native
  driver of Windows builds, where Vulkan is off by default (a build has
  one; a Windows build may take Vulkan instead). It is C over
  the COM interfaces' C form (`COBJMACROS`), against the DirectX headers
  kept as published in `directx/` and the Windows SDK's DXGI headers,
  and opens `d3d12.dll` and `dxgi.dll` from the system directory when
  the instance starts; without them, or without a DXGI factory, the
  instance has no D3D12 adapters. It uses whatever D3D12 runtime its
  process has, the in-box one or the Agility SDK the program's exe
  selects, and never selects one itself. A search lists every DXGI
  adapter that opens a feature level 12_0 device with shader model 6.0,
  WARP among them as a software adapter, under its LUID; opening a
  device there to read it is cheap, since D3D12 keeps one device per
  adapter while it is held. Limits are WebGPU's floor, raised where
  every device at the floor goes further (2D textures of 16384, 2048
  layers, a 128-byte root block within the root signature's 64 words).
  Every device grants timestamps, BC textures, 32-bit float filtering,
  rg11b10ufloat targets, dual-source blending, unclipped depth and
  indirect first instances, which feature level 12_0 requires; 64-bit
  integers and wave operations where D3D12 reports them; heaps on
  resource binding tier 3 (mrhi-0015). A device holds
  its D3D12 device and a direct command queue. CI runs the conformance
  suite on the hosted Windows runner's WARP under the Agility SDK's
  debug layer and GPU-based validation, which the suite's exe selects
  and whose errors fail it, and runs the samples there.
- **D3D12 objects:** buffers, textures, views, samplers and query sets
  sit in tables in the device's block, a handle being a slot plus one.
  Buffers and textures are committed resources in the default heap,
  starting in the common state; a buffer's size rounds up to 256 bytes,
  so that a uniform binding's constant buffer view stays inside it, and
  a texture whose views change its format, or a depth texture, is made
  in its typeless family. A view's slot holds, in a CPU-only descriptor
  heap, a shader resource view where it is sampled and an unordered
  access view of its first mip where it is stored to; a sampler's slot
  holds its sampler. Bindings copy them to the GPU's heaps. A 2D or cube
  view of a later layer is a one-layer or one-cube array, since only
  arrays name their first layer, and a stencil view reads the second
  plane. An occlusion query set is a query heap. A resource's memory is
  what `GetResourceAllocationInfo` says.
- **D3D12 pipelines:** a shader keeps a copy of each entry's DXIL and
  makes one root signature (version 1.1) for every pipeline of the
  container: root constants for the root block, the constants (in rows
  of four, as the constants' buffer declares them) and the vertex
  information where the container has them, then per table a
  descriptor table of its resources and one of its samplers, each
  binding a one-descriptor range at the register and space the D3D12
  map gives, visible to every stage, resource data volatile. A container
  whose root signature passes D3D12's 64 words is refused as
  unsupported. A pipeline's state is made at the call and answered at
  the next poll; it holds a reference on the root signature and a copy
  of the bindings and its constants' words, each constant's default
  unless given, so that it outlives its shader, and a fixed constant
  given another value is refused as unsupported. Vertex attributes are
  the TEXCOORD semantic of their location, as SPIRV-Cross names vertex
  inputs; alpha blend factors read alpha, and minimum and maximum blend
  with factors of one. The pipeline cache is empty for now.
- **D3D12 frames:** three frames run at once, each slot holding a
  command allocator and list, an upload buffer for its staging, which
  stays in the generic read state, and CPU-only heaps of render target
  and depth stencil views that its passes' targets take; one readback
  buffer, which stays in the copy destination state, takes every
  readback at its offset in the ring, and each finished frame copies
  its readbacks' ranges out. Frames signal one fence with their serial,
  so a poll reads one value; a removed device reads all ones, and its
  removal reason becomes the loss report's reason and message. A
  frame's transients are made at its submit and released when it
  finishes: placed where the core put them in the slot's heap, which
  grows as frames need, where resource heap tier 2 lets one heap hold
  buffers and textures, and committed on their own on tier 1. A barrier
  marked aliasing is led by an aliasing barrier, and a placed render
  target or depth texture is discarded whole at its first use, as D3D12
  asks before anything else touches it. The
  core's barriers move textures, per subresource, between the classic
  states their plan states map to, a transition between two unordered
  access uses being a UAV barrier. Buffers are the driver's to track:
  the plan gives no barrier between two reads, which D3D12's states
  tell apart, so each copy, binding and pass declaring it moves its
  buffer to the state it needs, gathering read states, an imported
  sealed buffer starting the frame in every read the sealed state
  allows, and a plan's barrier after a write on a buffer
  in the unordered access state is a UAV barrier. Every buffer starts a
  frame in the common state, which D3D12 decays buffers to when a list
  finishes. A copy with a buffer offset off D3D12's 512-byte placement
  is split into one copy per row of blocks. Clears are the targets'
  loads; discarding loads and stores keep the contents. A pass's
  multisampled targets resolve at its end. A destroyed object waits
  until the next frame submitted finishes.
- **D3D12 surfaces:** a surface is a Win32 window, its handle holding
  the window; every adapter presents there, DXGI's compositor taking
  another adapter's images. The caps offer 8-bit and 10-bit unorm
  buffers in sRGB, half floats in scRGB, and HDR10 where the window's
  monitor is in HDR; fifo, mailbox, and immediate where DXGI tears;
  opaque alpha, since only composition swapchains blend; images that
  render, sample and copy. A configured surface is a flip-discard
  swapchain of three images on the device's queue, its color space set
  from the color, DXGI's Alt+Enter turned off, its images stretched to
  the window, whose size never refuses one. A window holds one
  swapchain at a time, so configuring again and unconfiguring wait for
  the device's frames and release the old swapchain at once. Acquiring
  waits on the frame latency waitable object (two presents queued at
  most, a second at most) so that presenting never blocks, answers
  occluded for a minimized window and suboptimal for a window whose size
  is no longer the swapchain's; a frame presents its images after its
  work, at sync interval 1 for fifo and 0 otherwise, tearing for
  immediate.
- **D3D12 bindings and draws:** each slot has shader-visible heaps of
  resource and sampler descriptors: the program's heaps' regions first
  (mrhi-0015), then rings of as many resource descriptors as the
  frame's command records (each binding is one) and of samplers up to
  what D3D12's 2048 leave, a table of the samplers the last one wrote
  taking those again. A table writes its descriptors there together, a uniform buffer
  as a constant buffer view, a storage buffer as a raw unordered access
  view (read-only, a raw shader resource view), a texture as a view of
  the binding's shape, and sets them as its descriptor tables. Pipelines
  of one container share a root signature, so their tables and root
  block stay set between them; each pass sets its own. A pipeline
  reading heaps points its heap tables at the pass's heap as it is set. The buffers a
  bind point's tables, vertex and index buffers use move to the states
  they need at each draw or dispatch; vertex buffers are set at the draw,
  with the pipeline's strides. D3D12's vertex and instance ids leave out
  the first vertex and instance, so a pipeline whose vertex entry reads
  them takes them as root constants: set before a direct draw, and for
  an indirect one copied ahead of its arguments into the slot's scratch
  buffer, where the pipeline's command signature sets them. Occlusion
  queries are binary; a resolve copies the ones the frame wrote and
  zeros for the others.
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
