# The Vulkan driver

How the Vulkan driver (`driver_vulkan.c`, `vulkan_*.c`) meets the SPI, and the places where Vulkan's rules or its implementations bite. It is the native driver of Linux, Android and the BSDs, and an option on Windows.

## Loader and adapters

The driver compiles against the Khronos headers kept as published in `khronos/` and opens the loader when an instance starts (`libvulkan.so.1`, then `libvulkan.so` for Android, `vulkan-1.dll`, `libvulkan.1.dylib`), reading every function through `vkGetInstanceProcAddr`. With no loader, or no device that qualifies, the instance simply has no adapters. A physical device is listed when it reports Vulkan 1.3, a queue family with graphics and compute, dynamic rendering, synchronization2, timeline semaphores, buffer device addresses and descriptor indexing, and the Vulkan 1.0 features the floor uses (`fullDrawIndexUint32`, `imageCubeArray`, `independentBlend`, `sampleRateShading`, `depthBiasClamp`, `fragmentStoresAndAtomics`, `samplerAnisotropy`, `shaderStorageImageExtendedFormats`). Adapters are keyed by the physical device, so one found again keeps its id.

`vulkan_recipe.c` composes the `VkDeviceCreateInfo` for a device def, the extensions and the feature chain included, and is the only place that does: devices the driver opens itself and devices described for an OpenXR runtime to make are built from the same recipe.

## Limits that were easy to map wrongly

Two limits once rejected the Android emulator and many mobile devices, and the reasons are worth keeping:

- `bindingsPerTable` bounds a slot's number in a table (WebGPU's `maxBindingsPerBindGroup`). Vulkan bounds no binding number, so the driver reports the contract's 1000, not `maxPerStageResources`, which bounds how many resources a stage binds.
- `tablesPlusVertexBuffers` is the most a pipeline reaches. Vulkan bounds descriptor sets and vertex buffers apart, never together, so the driver reports their sum, or the contract's 24 when the sum is less.
- The five per-stage limits (sampled textures, samplers, storage buffers, storage textures, uniform buffers) are what one pipeline may bind at once, and `maxPerStageResources` counts them all plus the color attachments. Their sum is fitted under that bound by bringing the limits above a common cap down to it, none below the contract's floor, whose sum fits Vulkan's least bound of 128. Devices with a large bound keep their limits.

## Memory

Buffers and textures are suballocated with TLSF from device-local blocks ([memory.md](memory.md#gpu-memory-per-driver)). The bookkeeping is sized from the device's budgets when the device is made, so the driver allocates nothing later either.

## Pipelines

A shader is a module over the container's SPIR-V. A pipeline's layout comes from the reflection: a descriptor set per binding table up to the last one used, each slot a binding, and the root block as one push constant range for the pipeline's stages. Graphics pipelines use dynamic rendering, with the viewport, scissor, blend constant and stencil reference dynamic. Pipelines are made at the call and answered at the next poll. The device keeps one pipeline cache, which takes a blob only when Vulkan's own cache header names the device and its cache UUID.

## Frames

Each frame in flight has a slot: its command buffers, its transients made at submission and bound where the core placed them in the slot's block, its mapped staging, the readback ranges it fills, and descriptor pools that reset when the slot is reused. A frame signals the device's timeline semaphore with its serial, so a poll reads one counter, copies finished frames' readbacks into the core's ring and reports them in order. A frame also waits on the timeline for the frame that used its slot before it. The host has already seen that frame finish before reusing the slot, so the wait costs nothing, but it puts the reuse in the queue's order, where synchronization validation can see it. A command buffer that fails to begin or end stops the recording: nothing more is recorded into or ended on the frame's buffers, nothing is submitted, and the submission reports the failure (`test_vulkan_frame` fails each begin in turn).

Every pass class records in the one queue the device opens, in the frame's order, so the frame is one batch of command buffers: a native pass's own buffer goes between two of the driver's. The recording is still cut in lanes, one per class, ready for a queue per class: a pass starts a new batch on its lane when it follows a pass of another class (`waitFor` in the frame view), and a pass that another class follows ends its batch. With one lane, as every build but a test one has, the classes share it and the frame stays one batch. The test-only option `MAUL_RHI_VULKAN_SPLIT_LANES` gives each class its own lane, on the same queue: each lane signals its own timeline semaphore at the end of each batch, a batch waits for the values of the batches its first pass follows, the first batch resets the frame's query sets before every lane's first batch, and a last batch on the graphics lane waits for every lane and records the frame's end barriers. The batches go to the queue in the order they open, so each wait names a value already submitted.

What checks the lanes, each for its own part:

- The core's assertions check the `waitFor` edges themselves against every use in the frame ([frame-graph.md](frame-graph.md)).
- An assertion before each submission, in Debug and sanitizer builds, works the edges between lanes out again from the frame view and finds each in the submissions themselves: the batch holding a pass waits on the followed lane's semaphore for at least the value that the batch holding the followed pass, submitted before it, signals. A wait dropped in the recording or in the submission traps there.
- The `linux-lanes` cell runs every suite in the split build with those assertions on, under synchronization validation, which checks every barrier in the queue's order and the timeline semaphores' use.
- `hazards.pass_classes` reads back wrong values when a pass lands in a batch submitted before one it follows.

On one queue a barrier also waits for every earlier batch, so no check here sees whether a semaphore wait alone orders two lanes on the GPU. That, and a second queue family, wait for an adapter that has one ([#64](https://github.com/Rawframe-Project/maul-rhi/issues/64)).

The core's resource states map to Vulkan stages, accesses and layouts for its barriers. A barrier marked aliasing waits for all commands' writes. A copy destination's barriers name the clear stage beside the copy stage, since `mrhiPass_ClearBuffer` is `vkCmdFillBuffer` and the core's clear of a new texture is a clear command too.

The core's clear of a new texture runs in the transfer destination layout: `vkCmdClearColorImage` over every mip and layer of a color format, `vkCmdClearDepthStencilImage` of a depth format, and for a compressed format, which no clear takes, copies from a host-visible buffer of zeros the device keeps, in bands of as many rows of blocks as it holds: 64 KiB, or a row of blocks of the widest texture the device takes where that is more. So every image but a transient one is made with `TRANSFER_DST`; an adopted image is never cleared.

## Passes

A pass renders between `vkCmdBeginRendering` and `vkCmdEndRendering` over views of its targets, a resolve averaging samples. Its viewport covers the targets with a negative height, so that +Y points up in normalized device coordinates as on every driver, with the targets' origin at their top-left corner. Every draw state starts at its default. A table's bindings are written into a descriptor set taken from the slot's pools, and each recorded binding carries its slot's kind, so the driver knows the descriptor type without the reflection. The root block is pushed as constants to the pipeline's stages.

Counted multi-draw is `vkCmdDrawIndirectCount` (with `drawIndirectCount` and `multiDrawIndirect`); multiview is the rendering view mask.

## Queries

A query set is a query pool. Each set a frame names is reset once before its first pass, in every recording: a frame that failed to record is not submitted, so the next one, under the same serial, resets it again; a pass's timestamps are written before and after it, and occlusion queries bracket draws. As it records, the driver marks the queries the frame writes, and a resolve copies those with their results awaited and fills the others with zero, since Vulkan writes nothing for a query never written.

## Heaps

`bindlessSampling` is granted with descriptor indexing over sampled images and samplers (runtime arrays, partially bound and update-after-bind bindings updatable while pending, non-uniform indexing) and five bound sets; `bindlessHeterogeneous` adds the storage types' equivalents, storage images without a format, and `VK_EXT_mutable_descriptor_type`. The heap sizes are the update-after-bind limits less what four full tables and the color targets may take, since per-stage limits count every set.

A device has one set layout at set 4: binding 0 holds `heapSize` sampled images, or mutable descriptors that may also be storage images and buffers, and binding 1 `samplerHeapSize` samplers. Only a set's last binding may vary in count, so every heap takes the device's counts. Each heap is a set from its own pool. Pipelines from containers that read heaps have the layout at set 4, and the pass's heap is bound again after each pipeline, since other table layouts disturb it. A sealed texture is in `SHADER_READ_ONLY_OPTIMAL` with every shader stage's sampled reads.

## Surfaces

The platform headers of the same Khronos tag are kept beside `vulkan_core.h` and compiled on their platform only (XCB and Wayland on Linux and the BSDs, Win32, Android, Metal). The driver declares the three XCB types the XCB header names, so no window library is needed to build. The instance enables `VK_KHR_surface`, the platform surface extensions and `VK_EXT_swapchain_colorspace` its loader offers; a source without its extension, and a canvas, are unsupported.

An adapter presents to a surface when it offers `VK_KHR_swapchain` and its queue family presents there. Its colors are the surface's format and color space pairs the contract's rows map, an sRGB format standing for its unorm twin. A configuration makes a swapchain of at least three images, with `VK_KHR_swapchain_mutable_format` for twin views; a retired swapchain is destroyed once the queue is idle. Each swapchain has a semaphore per image, signalled by the frame for its present, and one acquire semaphore more than its images, each reused once the frame that waited on it finishes. A frame waits on its images' acquires, which its first transitions wait for, and presents after its submission. A present's result is reported at the next acquire, and a window with no area is occluded.

## Labels

The instance enables `VK_EXT_debug_utils` where the loader offers it, which costs nothing unless a tool listens. Objects are named from their defs' labels, each labelled pass is a labelled region around its barriers and work, and debug groups and markers become command labels. Labels are copied with a terminating NUL, as Vulkan reads them.

## Interop

Adopted instances, devices and images, the native pass and the feature chain merge are described from the program's side in [native interop](../user/native-interop.md). Inside the driver: an adopted instance's functions all come through the adopted `vkGetInstanceProcAddr`, surfaces and labels are used only where the adopted extensions name them, swapchains are enabled on a device only where the instance has surfaces (as `VK_KHR_swapchain` requires `VK_KHR_surface`), and an adopted swapchain image costs a frame that renders to it and then reads it one more barrier, back to the target layout, at its end. A native pass ends the driver's command buffer with every barrier into the pass ordered against all commands and memory access, submits the program's buffer, and begins the next one with a barrier that orders everything after it behind the program's writes.
