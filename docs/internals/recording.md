# Recording

What happens between a frame's compile and its submission: how passes record on several threads into memory the device sized up front, what each command is checked against, and how uploads, readbacks and queries fit the same budget.

## Why record into an arena

Vulkan and D3D12 record command buffers on any thread; Metal makes encoders from one command buffer per queue; WebGPU encodes on one thread and checks every command against its pass. The library allocates nothing after a device is made, starts no threads and takes no callbacks. So commands are recorded into the library's own memory, bounded when the device is made, and each driver translates them at submission into its API's form. A side effect is that the WebGPU driver, which must make its JavaScript calls on one thread, sees the same parallel recording as the others.

## Claiming a pass

Passes record between `mrhiCompileFrame` and `mrhiSubmitFrame`. `mrhiBeginPass` claims a kept pass with an atomic exchange, so a pass is begun once, by one thread; a culled pass, a pass of another frame and a pass already begun are refused, and submission is refused while any pass still records. Different passes record on different threads at once. A kept pass that is never begun records nothing but still loads and stores its targets.

## The arena

Each frame's commands live in the device's `frameCommandBytes` (at least 4 KiB), handed out in 4 KiB chunks by one atomic counter. A command is a 32-byte record, and its payload (root block bytes, a viewport, a color, a label, a binding table) follows it in the same chunk. Each pass links its own chunks, so passes on different threads share nothing but the counter: one atomic operation per 4 KiB.

A command that finds no chunk is refused with `mrhi_errorCapacity` and marks its pass, which records nothing more, and the frame's submission is then refused as capacity. A command is never dropped silently. Because a table and its command must fit one chunk, a table holds at most `MRHI_TABLE_BINDINGS` bindings. Debug group pushes and pops that are refused for capacity still count, so a pass that filled the arena can still end balanced.

## What each command is checked against

Every command is checked as WebGPU checks it, so that a frame that records on any driver records on the web:

- **The pass's kind.** A pass with targets draws, a graphics or async compute pass without them dispatches, and only a pass without targets copies. A native pass records nothing through the encoder.
- **Pipelines.** The pipeline set is live, of the pass's kind and ready; a graphics pipeline's color formats, depth format, sample count and view count equal the pass's targets, and it writes neither depth nor stencil into a read-only depth target. A pipeline whose shader reads a heap needs a pass with a heap.
- **State.** Viewports, scissors, blend constants and root block writes (4-byte aligned, within `rootBlockBytes`) are checked before they are recorded. The root block is not tracked across draws: each pass starts with it zeroed, which drivers ensure.
- **Draws and dispatches** need the pass's pipeline; every table its reflection has bindings in, set under a container of the same digest; every vertex buffer it reads, holding the vertices and instances drawn; for an indexed draw an index buffer holding the indices, of the pipeline's strip index format if it has one; workgroup counts within the limit. Something not set is `mrhi_errorState`; something too small is invalid.
- **Indirect arguments** (16, 20 or 12 bytes at an offset that is a multiple of 4) lie in a buffer the pass declares with the indirect access. They are read on the GPU, so nothing checks their values: as in WebGPU, a draw with a nonzero first instance without `indirectFirstInstance`, or a dispatch past the limit, does nothing, and a driver whose API does not ensure that validates on the GPU.
- **Copies** follow WebGPU's rules: aligned offsets and sizes, layouts given where needed and fitting, regions in whole blocks within a mip's physical size, only the aspects WebGPU copies, depth and multisampled textures whole, copy-compatible formats. The pass declares a copy source access covering what is read and a copy destination access covering what is written, so no part is both.
- **Declared access.** Every resource a command touches needs an access its pass declared, of the kind the use needs. The declarations stay the only source of hazards, so a command can never introduce one the plan did not see.

An empty copy, write or read records nothing: WebGPU allows them, while Vulkan refuses a zero-size copy. A read is still answered, with no bytes.

## Uploads and readbacks

- **Uploads** (`mrhiWriteBuffer`, `mrhiWriteTexture`) copy the program's bytes at the call into the frame's staging region, the device's `frameUploadBytes` for each frame in flight, and record a copy from it. Every upload starts at a 512-byte boundary and a texture's rows at a 256-byte pitch, the strictest of the four APIs, so no driver repacks. Each running frame keeps its own region, chosen free when the frame begins, whatever order frames finish in. A full staging marks the pass as a full arena does.
- **Readbacks** (`mrhiReadBuffer`, `mrhiReadTexture`) copy into the device's readback ring, `readbackBytes` at the same alignments, and each takes one of `readbacks` records and a request. A readback wraps to the ring's start rather than split (the bytes an empty ring skips are free at once, so an empty ring always holds a readback of its whole size), and is refused for capacity when the ring, the records or the answer room for it and its frame run out. The frame's finish queues `mrhi_deviceReadbackReady` for each, after the driver filled the ring. `mrhiTakeReadback` copies the bytes out with rows packed tightly, and taking frees the ring in order from the oldest, so a readback left untaken holds the ring.
- Readbacks are taken and recorded under a lock, the one lock the encoders hold, since passes record in parallel and records must stay in order. A dropped frame or a failed submission gives back its records and ring bytes.

## Queries

Query sets are device objects with a type, occlusion, timestamp or pipeline statistics, and a count of 1 to 4096 (WebGPU's bound). The device limits `querySets` (16) and `queries` (4096 across all sets) bound them; each query keeps an 8-byte mark in a table the device allocates once, and a set takes a contiguous run of it, first fit.

- **Where queries go.** Occlusion and statistics queries are written in graphics-class passes, and every resolve is, where declaration order is execution order. Timestamps are written by passes of every class. A resolve is recorded after the compile, so the compile cannot see which sets it reads: a pass that declares a query resolve access follows every class's timed passes before it, and a timed pass follows every such pass before it ([frame-graph.md](frame-graph.md#compiling)). A resolve then sees what earlier passes wrote, on any queue, without query sets becoming frame resources. On D3D12 a timed pass never runs on the copy queue ([d3d12.md](d3d12.md#queues)), so copy-queue timestamps, which D3D12 keeps in a heap type of their own, never arise.
- **Once a frame.** A query is written at most once in a frame, stricter than WebGPU's once a pass. A driver then resets each set once before the frame's first use, as Vulkan requires. The check is one atomic exchange of the query's mark with the frame's serial, exact while passes record in parallel.
- **Timestamps** are written at pass boundaries, the one form WebGPU allows. A pass that is not one WebGPU pass, such as copies beside dispatches, gets its times from empty compute passes that write them. A culled pass writes none. Each pass's end is no earlier than its start, but the frame promises no order between two passes' times, even when one follows the other: Vulkan writes a start at the top of the pipe without waiting for an earlier barrier, so a later pass may start, by its time, before an earlier one ends. The conformance case checks only each pass's own pair.
- **Pipeline statistics** bracket work in one graphics-class pass rendering one view. The set is named at the begin, not on the pass def, since neither Vulkan nor D3D12 needs it when the pass begins. They are refused in async compute passes, since Vulkan asks a graphics-capable pool of a query counting graphics stages, and in multiview passes, over whose views Vulkan would spread a query. Each resolves to eleven 64-bit counters in Vulkan's bit order and D3D12's struct order, which are the same; the geometry and tessellation counters are zero, since the library has neither stage.
- **Resolving** writes 64-bit values into a buffer the pass declares with the query resolve access, at a multiple of 256. A query the frame did not write resolves to zero, which each driver ensures from the stream, since Vulkan writes nothing for a query never written.

## Debug labels

Pass labels are copied into a table of `framePasses` labels when the pass is added, and debug groups and markers are commands. In a build with `MAUL_RHI_LABELS=OFF` every label is still checked, so a call refused in one build is refused in every build, and then dropped: defs reach the driver without labels, passes keep none, the frame allocates no label storage, and markers record nothing. Group balance is still counted, so an unbalanced pass is refused in both builds.
