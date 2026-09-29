# mrhi-0011. Encoders: passes recorded into a frame's arena

Status: Accepted

## Context

A compiled frame (mrhi-0008) is recorded by the program, pass by pass,
possibly on several threads. Vulkan and D3D12 record command buffers on
any thread, Metal encoders are created from one command buffer per
queue, and WebGPU encodes on one thread and validates every command
against its pass. The library allocates nothing after a device is made,
runs no threads and takes no callbacks, so the commands a program
records need a home it bounds up front, and a real driver translates
them at submission.

## Decision

- **Phase:** passes record between the frame's compile and its
  submission. `mrhiBeginPass` claims a kept pass with an atomic
  exchange, so a pass is begun once, by one thread; `mrhiEndPass` ends
  it. A culled pass, a pass of another frame and a pass already begun
  are refused, and so is a submission while a pass still records. A
  kept pass never begun records nothing.
- **Arena:** each frame's commands live in the device's
  `frameCommandBytes`, at least 4 KiB, in 4 KiB chunks taken with one
  atomic counter. A command is a 32-byte record, and its payload (root
  block bytes, a viewport, a color, a label) follows it in the same
  chunk. Each pass links its own chunks, so passes on different threads
  share nothing but the counter.
- **Full arena:** a command that finds no chunk is refused with
  `mrhi_errorCapacity` and marks its pass, which records nothing more;
  the frame's submission is then refused as capacity. A command is never
  dropped silently.
- **Checks:** every command is checked against its pass, as WebGPU
  checks it:
  - a pass with targets draws, a pass without them dispatches, and a
    transfer pass copies;
  - a pipeline set is live, of the pass's kind and ready; a graphics
    pipeline's color formats, depth format and sample count equal the
    pass's targets, and it writes neither depth nor stencil into a
    read-only depth target;
  - viewports, scissors, blend constants, root block writes (4-byte
    aligned, within `rootBlockBytes`, else unsupported) and debug labels
    are checked before they are recorded.
- **Binding tables:** `mrhiSetBindings` sets one whole table: exactly
  its slots in the reflection of the pass's pipeline, which is set first
  and looked up by id again on each call. A binding is a buffer range, a
  texture of the frame seen through an inline view, or a sampler, each
  checked as WebGPU checks a bind group entry (alignment, size limits
  and minimums; view kind, format, sample type and multisampling;
  sampler kind). Each resource also needs a declared access of the pass
  covering it, of the kind the slot needs: the declarations stay the
  only source of hazards. A table fits one chunk with its command, so a
  shader's table holds at most `MRHI_TABLE_BINDINGS` bindings.
- **Draws and dispatches** need what WebGPU needs, else they are
  refused: the pass's pipeline; every table its reflection has bindings
  in, set under a container of the same digest, so identical containers
  stay compatible; every vertex buffer it reads, each holding the
  vertices and instances drawn (the last element only its attributes'
  bytes), and for an indexed draw an index buffer holding the indices,
  of the pipeline's strip index format when it has one; and workgroup
  counts within `workgroupsPerDimension`. Something not set is a state
  refusal, something too small invalid. The root block is not tracked:
  each pass starts with it zeroed, which the driver ensures.
- **Indirect draws and dispatches** (`mrhiDrawIndirect`,
  `mrhiDrawIndexedIndirect`, `mrhiDispatchIndirect`) check the same
  state and that their arguments, 16, 20 or 12 bytes at an offset that
  is a multiple of 4, lie in a buffer the pass declares with the
  indirect access. The arguments are read on the GPU, so nothing checks
  them here: as in WebGPU, a draw whose first instance is not zero
  without `indirect_first_instance`, or a dispatch past
  `workgroupsPerDimension`, does nothing, which a driver whose API does
  not ensure it validates on the GPU.
- **Copies** (`mrhiCopyBuffer`, `mrhiCopyBufferToTexture`,
  `mrhiCopyTextureToBuffer`, `mrhiCopyTexture`) are recorded in passes
  without targets and checked as WebGPU checks them: aligned offsets
  and sizes, buffer layouts that are given where needed and fit, regions
  in whole blocks within a mip's physical size, only the depth and
  stencil aspects WebGPU copies, depth formats and multisampled textures
  copied whole, and copy-compatible formats between textures. Formats
  carry their copy facts in the contract. The pass must declare a copy
  source access covering what is read and a copy destination access
  covering what is written, so one part is never both.
- **Uploads** (`mrhiWriteBuffer`, `mrhiWriteTexture`) copy the
  program's bytes at the call into the frame's staging, the device's
  `frameUploadBytes` for each frame in flight: every upload at a 512-byte
  boundary and a texture's rows at a 256-byte pitch, so that every API
  copies from staging without repacking. They are checked as WebGPU
  checks its queue writes and recorded as copies from the staging. A
  full staging marks the pass as a full arena does. Each running frame
  keeps its own region, chosen free when a frame begins, whatever order
  frames finish in.
- **Readbacks** (`mrhiReadBuffer`, `mrhiReadTexture`) copy into the
  device's readback ring, `readbackBytes` at the same alignments as
  staging, and each takes one of `readbacks` records and a request. A
  readback wraps to the ring's start rather than splitting, and is
  refused for capacity when the ring, the records, or answer room for
  it and its frame run out, since the frame's finish queues
  `mrhi_deviceReadbackReady` for each. The program takes the bytes
  once with `mrhiTakeReadback`, rows packed tightly; taking frees the
  ring in order from the oldest, so a readback left untaken holds the
  ring. Readbacks are taken under a lock, the one lock the encoders
  hold, as passes record in parallel and records must stay in order. A
  dropped frame or a failed submission gives back the frame's records
  and ring bytes.
- **Debug groups** balance by the pass's end. A push or pop refused for
  capacity still counts, so a pass that found the arena full still
  ends.

## Consequences

Recording scales across threads with one atomic operation per 4 KiB,
and a program sizes the arena once. A driver reads each pass's chunks in
order at submission and never meets an unchecked command. The arena is
per device, so a device records one frame at a time, as mrhi-0008
already requires.
