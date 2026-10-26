# The Metal driver

How the Metal driver (`driver_metal.m`, `metal_*.m`) meets the SPI. It is the native driver of macOS and iOS builds, where the Vulkan driver is off, since a build has one native driver.

## Objective-C in plain C structs

The driver's files are Objective-C in the family's C23 dialect, compiled with manual retain and release (`-fno-objc-arc`), so that Metal objects sit in plain C structs and are released exactly where the SPI destroys them. Every entry point that touches Objective-C objects drains its own autorelease pool, because the library runs on the program's threads and cannot rely on an outer pool.

It speaks Metal's classic API (command queues, command buffers and encoders), which every Metal device runs, the paravirtual device of hosted macOS runners included. A path through Metal 4's argument tables can come inside the driver where a device reports that family, without a change above it.

## Adapters and limits

A search lists every Metal device that meets the floor (the default device outside macOS), under its registry id. The floor's cube array textures exist from the Apple4 family, the A11 chip, and on every Mac, matching what the Vulkan driver asks with `imageCubeArray`; the iOS simulator's GPU has none, so it lists no adapter. Limits are WebGPU's floor, raised where Metal's feature set tables promise more for the device's family and where the device reports its own. Features are granted as the driver comes to run them.

Not granted yet: bindless heaps, which need `MTLGPUFamilyMetal3` (descriptors written into a plain buffer, which SPIRV-Cross emits from MSL 3.0), and timestamps, which need the timestamp counter set. The hosted macOS runner's device has neither, and the driver grants nothing CI cannot run. Pipeline statistics are never granted: only some Macs have a statistic counter set, sampled at encoder boundaries.

On iOS, the driver uses what later versions add where they have it: BC compression from iOS 16.4, extended-range surfaces from iOS 16. Presenting immediately is macOS's alone.

## Objects

Buffers, textures, views, samplers and query sets are Metal objects whose retained pointers are their handles, released as soon as the core destroys them: Metal's command buffers retain what they use, so no retirement queue is needed for them.

- Buffers and textures live in private memory; a transient render target is memoryless on Apple family GPUs.
- The depth and stencil format is `Depth32Float_Stencil8`, whose stencil a view sees as `X32_Stencil8`.
- An occlusion query set is a buffer of 8 bytes per query, the render passes' visibility results.
- Samplers are made to sit in argument buffers.
- A resource's memory is Metal's heap size and alignment for it.

## Pipelines

A shader holds a library per entry point, made from the container's metallib when it has one and otherwise compiled from each entry's MSL without fast math, and a copy of the Metal map. Each entry is the function of its own name, specialized with the pipeline's constants as function constants of their ids. Pipelines are made at the call and answered at the next poll, and keep what their frames bind, so they outlive their shader.

Vertex attributes take their locations as attribute indices, and vertex buffer n lies at buffer index 30 minus n. Refused as unsupported: a pipeline whose vertex buffers reach a buffer index its container uses; a sample mask that leaves samples out, since Metal has none; a workgroup larger than the pipeline's threads. A buffer of stride 0 takes Metal's constant step function.

## Frames

A frame is recorded at submission into command buffers on its lanes (below): a render encoder for a pass with targets, a compute encoder for one without, a blit encoder for a transfer pass. Copies and resolves in a compute pass leave it for a blit encoder, and the next compute encoder takes up the pipeline and state again, the pass's debug groups closed and reopened per encoder. Bindings, the root block and SPIRV-Cross's buffer sizes are applied through the pipeline's map before each draw or dispatch.

Metal tracks hazards between encoders on a queue itself, so the core's barriers need no commands. Uploads are copied into the slot's shared staging buffer, and readbacks land in a shared mirror of the ring, copied into it once the frame finishes. The query sets a frame resolves are cleared first, so a query it does not write reads zero. A poll reads the status of every command buffer the frame committed; a waiting program waits on a semaphore the last completion signals; a failed command buffer is the device's loss. A destroyed pipeline is released after the next frame's commit.

`mrhiClearBuffer` is `fillBuffer:range:value:`. The core's clear of a new texture is a render pass that clears each mip and layer, ending the pass's blit encoder and opening a new one after, for a depth texture, its depth and stencil in one pass, and for a render target of one layer that is not a volume, where it measured 3.6 times faster than a copy at 4096 by 4096 RGBA8 on the Apple Paravirtual device ([#135](https://github.com/Rawframe-Project/maul-rhi/issues/135)). Every other texture takes blit copies from a 64 KiB shared buffer of zeros, in bands of rows of blocks, or parts of a row where a row is longer, which measured faster than a pass per layer or depth slice: 1.8 against 2.6 ms for 16 layers of 1024 by 1024, and 2.5 against 37 ms for 256 depth slices. So every depth texture is made with `MTLTextureUsageRenderTarget`. Counted multi-draw is emulated: a compute pass of the library's copies the records into scratch, every record past the count drawing no instances, then `maxCount` indirect draws run from the scratch. Multiview is vertex amplification onto layers, as many views as the device amplifies to; the container's MSL for a view-index entry reads the amplification id (vertex) or the render target array index (fragment), which `tools/mrhi_msl.py` substitutes for SPIRV-Cross's constant view index. Draws and indirect records stay as they are, where instanced views would have changed every vertex entry and every indirect draw.

## Surfaces

A surface is a retained `CAMetalLayer`, and every adapter presents to it: 8-bit sRGB in Rec. 709 and Display P3, and linear half floats of standard or extended range in both; fifo, and immediate on macOS; opaque or premultiplied alpha. Configuring sets the layer's device, pixel format, color space, extended range, drawable size, synchronization and opacity. The program's size is its own, since the layer scales drawables to its bounds. An acquire takes the next drawable: out of date when something else resized the drawables, suboptimal while the layer's bounds in pixels differ. A frame presents each image it acquired after its work.

## Queues

A device holds three command queues, one per lane (`metal_lanes.m`): the graphics queue, which `mrhiGetMetalDevice` returns, and an async compute and a transfer queue beside it, labelled as the device is with the lane's name after. A pass runs on its class's queue, except that a pass touching a surface image runs on the graphics queue, where the frame presents. Metal's queues are not typed, so every other pass can run where its class says.

The lanes are ordered with an `MTLEvent` per lane. A pass follows the passes its `waitFor` names and the pass of its class before it. A lane's passes go in the frame's order into batches: a new batch where a pass follows a pass on another lane, and the batch ends after a pass that another lane follows. A batch is one command buffer, committed when it ends. It first waits for the other lanes' batches its first pass follows. It then signals its lane's event with the lane's next value.

Metal updates an event once the command buffer's earlier commands have finished, not the queue's, so a signal alone does not show that the lane's earlier command buffers have finished. Each batch therefore also waits for its lane's batch before it, which makes each signal stand for all of the lane's work up to it. Each lane's first batch of a frame waits for every event's value before the frame, so a frame's work follows all of the frame before. A wait names only batches committed before it, so no wait can block for good.

That a second queue overlaps the first is not measured: CI's macOS runners have the Apple Paravirtual device, and `test_metal_queues` checks the batches, their waits and their signals, not the timing.

## Native passes

`mrhiSetMetalPassCommands` takes a command buffer from the device's graphics queue with no encoder open, not committed. The program's command buffer can take no wait at its start, so the driver commits its graphics batch, then a command buffer that only waits for what the native pass follows and for that batch, then the program's. This relies on the graphics queue not running past a wait until it is met, which Apple's [Synchronizing events within a single device](https://developer.apple.com/documentation/metal/synchronizing-events-within-a-single-device) states: "When each queue reaches a command that waits for an event, that queue blocks further execution until the event is signaled." The method's own reference, [`encodeWaitForEvent(_:value:)`](https://developer.apple.com/documentation/metal/mtlcommandbuffer/encodewaitforevent(_:value:)), promises less: it "pauses the GPU from running the buffer's subsequent passes". If only that narrower reading holds, the program's command buffer may start before the work it follows on the async compute and transfer queues has finished, and race with it, and a second native pass's command buffer may likewise start, and signal, before the first's has finished; work on the graphics queue it shares a resource with stays ordered by Metal's hazard tracking. Every later batch stays ordered either way, since each waits for the native event and for the graphics batch before the native pass. The driver encodes a signal of a native event at the end of the program's command buffer before committing it, and every later batch that follows the native pass waits for that signal. A frame is done when every command buffer it committed has completed, the program's included.

## Debugging

CI runs the conformance suite and the samples on the macOS runner's device under `MTL_DEBUG_LAYER` and `MTL_SHADER_VALIDATION`, which stop a test on misuse. On the macOS 26 image, shader validation's texture usage check reports a mismatch on every texture a shader reads and zeroes the read; `test/metal_usage_check.m` shows it with no library code, and where it does, CI runs without that one check (`MTL_SHADER_VALIDATION_TEXTURE_USAGE=0`). What the check would prove there, `test_metal_native` checks directly: each texture's `MTLTextureUsage` holds what its core usage allows (`UsageOf` in `metal_resource.m`), and the core refuses any access a texture's usage does not allow.
