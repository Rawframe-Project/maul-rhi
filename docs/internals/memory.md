# Memory

Where the library's own memory comes from, how GPU memory is placed on each driver, and why nothing allocates once a device exists.

## One block per owner

An instance and each device take their memory once, when they are made, through the allocator in their def, as one block laid out from their limits. `src/allocator.c` lays a block out in parts with checked arithmetic (`mrhiLayout`): a limit so large that a part's size overflows is refused as capacity, never wrapped. Every table the owner needs (object slots, notification rings, diagnostic queues, the frame's resources, passes, accesses, barriers, the command arena, staging and the readback ring, the query marks, the frame view a driver reads) is a part of that block, so the owner's footprint is known from its def and `maul-rhi_bench` prints it exactly.

The reason is the family's: a program sizes its budgets once, a call past one answers `mrhi_errorCapacity`, and hot paths (recording, submitting, polling) never allocate, lock the allocator or fail for memory. A zeroed allocator means the C library's, which serves alignments up to `max_align_t`'s; a larger alignment needs the program's own allocator.

## Ids

Every object a root owns is a slot in a fixed pool (`src/pool.c`): a free list, and a generation per slot that each release bumps. An id is the slot's index plus one and the generation, so 0 is null and an id kept past its object's end is refused with `mrhi_errorStale`, at the cost of one array read and a compare. Driver handles are never identities: the core maps its ids onto the 64-bit handles a driver returns, and recorded commands name pipelines, samplers and query sets by handle, so a slot freed and taken again while a frame records never changes what the frame names.

## A frame's memory

Transient resources are placed in one frame memory by lifetime: first fit, in first-use order, with the sizes and alignments the driver reports for each, so resources whose uses do not overlap share bytes. A transient render target that is written and discarded within its pass needs no memory on tile GPUs, where the driver reports none. A texture listing view formats is never transient, because WebGPU's transient attachments take none. `mrhiGetFrameMemory` reports the total; [frame-graph.md](frame-graph.md#aliasing) covers the barriers aliasing needs.

Uploads go into the frame's slot of the staging (`frameUploadBytes` for each frame in flight), each at a 512-byte boundary with texture rows at a 256-byte pitch, the strictest of the four APIs' copy rules, so every driver copies from staging without repacking. Readbacks land in the device's ring at the same alignments. [recording.md](recording.md#uploads-and-readbacks) has the rules.

## GPU memory per driver

- **Vulkan** suballocates buffers and textures itself, with TLSF (two-level segregated fit, after Masmano, Ripoll, Crespo and Real, "TLSF: a New Dynamic Memory Allocator for Real-Time Systems", ECRTS 2004; the exact-fit scan of a size's own class is the library's addition) from device-local blocks per memory type and kind. Buffers and textures keep separate pools, so `bufferImageGranularity` never applies. A block is 64 MiB, or an eighth of its heap if that is smaller; a resource the driver wants alone, or one over half a block, gets its own allocation; an empty block is freed unless it is its pool's last. Each frame slot keeps one block for its transients, grown when a frame needs more, and mapped staging and readback memory come with the device.
- **Direct3D 12** makes device buffers and textures as committed resources in the default heap. A frame's transients are placed resources in the slot's heap, which grows as frames need, where resource heap tier 2 lets one heap hold buffers and textures; on tier 1 each is committed on its own. A resource's size and alignment are what `GetResourceAllocationInfo` says.
- **Metal** keeps buffers and textures in private memory, as the frame graph fills and reads them; a transient render target is memoryless on Apple family GPUs. Declared resources are made per frame and released once the command buffer that holds them is committed.
- **WebGPU** places memory itself. Declared resources come from a pool keyed by their descriptors, each its own object and never aliased; one no frame took in the last 8 is destroyed. A frame's reported memory is an estimate there.

Destroyed objects are retired, not freed at once: a driver keeps each until the next frame submitted after its destruction has finished, since frames are translated at submission and may still name it. The Vulkan driver's tables hold twice the objects the core allows, so a program may replace each object once between submissions.
