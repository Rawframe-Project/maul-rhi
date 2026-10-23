# Resources and frames

All GPU work reaches a device as a frame. A frame declares its resources and passes up front; the library culls what nothing needs, plans the barriers and places transient memory, the program records each pass, and the driver translates the frame at submission. There are no barriers, states, fences or semaphores in the API, so a hazard cannot be written.

## Resources

**Device objects** outlive frames: buffers (`mrhiCreateBuffer`), textures (`mrhiCreateTexture`) and their views (`mrhiCreateView`), samplers and query sets. A texture's usages are fixed when it is made, and its view formats may name only its sRGB or linear twin. Destroying an object that a submitted frame still uses is safe: the driver keeps it until that frame has finished.

**Transient resources** live for one frame: `mrhiDeclareBuffer` and `mrhiDeclareTexture` describe them, and the compile places them in the frame's memory, overlapping those whose uses do not overlap. A render target that is only written and then discarded stays on chip on tile GPUs. A texture that lists view formats is never transient.

A device object joins a frame through `mrhiImportBuffer` or `mrhiImportTexture`. An imported object carries its state from frame to frame, and is never aliased.

## A frame, step by step

```c
// Draws a triangle in a color its pipeline's root block takes into a
// 64x64 target the frame alone holds, and reads the target back: the
// pixels answer *pixelsOut once the frame *tokenOut has finished.
static bool DrawAndRead(mrhiDevice* device, mrhiGraphicsPipelineId pipeline, const float color[4],
                        mrhiRequestId* pixelsOut, mrhiRequestId* tokenOut)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    if (mrhiBeginFrame(device, &frame) != mrhi_success)
    {
        return false;
    }

    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8Unorm;
    targetDef.width = 64;
    targetDef.height = 64;
    mrhiResourceId target = {0};
    bool built = mrhiDeclareTexture(device, &targetDef, &target) == mrhi_success;

    mrhiPassDef drawDef = mrhiDefaultPassDef();
    drawDef.colorTargets[0] =
        (mrhiColorTarget){.resource = target, .load = mrhi_loadClear, .store = mrhi_storeKeep};
    drawDef.colorTargetCount = 1;
    mrhiPassId draw = {0};
    built = built && mrhiAddPass(device, &drawDef, &draw) == mrhi_success;

    const mrhiAccess read = {.resource = target,
                             .kind = mrhi_accessCopySource,
                             .range = {.mipCount = 1, .layerCount = 1}};
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    built = built && mrhiAddPass(device, &readDef, &reading) == mrhi_success &&
            mrhiCompileFrame(device) == mrhi_success;

    const mrhiTextureCopy source = {.resource = target};
    const mrhiExtent3d extent = {64, 64, 1};
    bool recorded = built && mrhiBeginPass(device, draw) == mrhi_success &&
                    mrhiSetGraphicsPipeline(device, draw, pipeline) == mrhi_success &&
                    mrhiSetRootBlock(device, draw, 0, color, 4 * sizeof(float)) == mrhi_success &&
                    mrhiDraw(device, draw, 3, 1, 0, 0) == mrhi_success &&
                    mrhiEndPass(device, draw) == mrhi_success &&
                    mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &extent, pixelsOut) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success;
    if (!recorded)
    {
        // Nothing of the frame runs.
        (void)mrhiDropFrame(device);
        return false;
    }
    return mrhiSubmitFrame(device, tokenOut) == mrhi_success;
}
```

**Declaring.** A device has one open frame at a time. Each pass has a class (`mrhi_passGraphics`, `mrhi_passAsyncCompute` or `mrhi_passTransfer`), its render and depth targets with their load and store operations, and every other access it makes (`mrhiAccess`: a resource, how it is used, which mips and layers). Within a pass a part of a texture is used in read-only ways or in one writable way, as WebGPU's usage scopes require. Passes of one class run in the order they are added.

**Compiling.** `mrhiCompileFrame` culls passes whose results nothing reads, unless a pass sets `neverCull`; chooses each target's store, keeping it only when a later kept pass reads it or the texture is imported; plans the barriers per mip, layer and plane; and places the transients. `mrhiIsPassKept`, `mrhiGetPassPlan`, `mrhiGetResourcePlan`, `mrhiGetFrameBarriers` and `mrhiGetFrameMemory` show the result, for tests and graph tools.

**Recording.** Between `mrhiBeginPass` and `mrhiEndPass`, the encoder calls record a kept pass: pipelines, bindings, the root block, dynamic state, draws, dispatches, copies, debug groups and queries. Passes may record on several threads at once, each pass on one. Every call is checked against WebGPU's rules and against the pass's declarations, on every driver: a binding, copy or draw that touches a resource the pass did not declare is refused. A kept pass that is never begun records nothing, but still loads and stores its targets.

**Submitting.** `mrhiSubmitFrame` returns a token that `mrhi_deviceFrameDone` answers in the device's queue. `mrhiWaitFrame` waits for it with a timeout where the platform allows blocking; on the web it never blocks. `mrhiDropFrame` abandons an open frame: nothing of it runs.

## Moving bytes

- **Uploads.** `mrhiWriteBuffer` and `mrhiWriteTexture` copy the program's bytes at the call into the frame's staging, which `frameUploadBytes` sizes for each frame in flight, and record a copy from it. The program's memory is free again when the call returns.
- **Readbacks.** `mrhiReadBuffer` and `mrhiReadTexture` copy into the device's readback ring (`readbackBytes`). Once the frame has finished, `mrhi_deviceReadbackReady` answers, and `mrhiTakeReadback` copies the bytes out, rows packed tightly. Take every readback: the ring frees in order from the oldest, so one left untaken holds the ring.
- **Clears.** `mrhiClearBuffer` zeros a range of a buffer, without staging, in a pass that declares it a copy destination: counters, indirect arguments and a culling pass's output start this way.
- **Copies.** `mrhiCopyBuffer`, `mrhiCopyBufferToTexture`, `mrhiCopyTextureToBuffer` and `mrhiCopyTexture` record in passes without targets, checked as WebGPU checks them. An empty copy, write or read records nothing; a read is still answered, with no bytes.

A full command arena, staging or ring refuses the call with `mrhi_errorCapacity` and marks its pass; the frame's submission is then refused too, so nothing is dropped silently. Raise the device limit and try again on the next frame.

## Draws beyond the basics

- **Indirect draws and dispatches** read their arguments from a buffer the pass declares with the indirect access. As in WebGPU, the arguments are read on the GPU and not checked: a draw with a nonzero first instance on a device without `indirectFirstInstance`, or a dispatch past the limit, does nothing.
- **Counted multi-draw** (`multiDrawIndirectCount`): `mrhiDrawIndirectCount` and `mrhiDrawIndexedIndirectCount` make up to `maxCount` draws from packed records, the number drawn read on the GPU from a count a culling pass wrote. A frame's `maxCount`s together stay within the device limit `frameIndirectDraws`.
- **Multiview** (`multiview`): a pass's `viewCount` renders it into that many consecutive layers of its targets, with pipelines made for as many views. Shader entries read the view index, so their containers carry no WGSL. The adapter limit `multiviewViews` bounds the views: four on Direct3D 12, often two on Metal.

## Queries

Query sets hold occlusion results, timestamps or pipeline statistics, at most 4096 queries each.

- **Occlusion:** a render pass names its occlusion set; `mrhiBeginOcclusionQuery` and `mrhiEndOcclusionQuery` bracket draws, one query at a time. A nonzero result means some sample passed.
- **Timestamps** (`timestampQuery`): a pass names its timestamp set and the queries written at its start and end. Values are ticks; `mrhiGetDeviceTimestampPeriod` gives nanoseconds per tick. Timestamps are written at pass boundaries only, the one place WebGPU allows.
- **Pipeline statistics** (`pipelineStatisticsQuery`, Vulkan and Direct3D 12): `mrhiBeginStatisticsQuery` and `mrhiEndStatisticsQuery` bracket work in one graphics-class pass; each resolves to eleven 64-bit counters.

Queries are written and resolved in graphics-class passes, and each query is written at most once a frame. `mrhiResolveQueries`, in a graphics pass without targets, writes 64-bit values into a buffer the pass declares with the query resolve access, at an offset that is a multiple of 256; a query the frame did not write resolves to zero. Read the values back with `mrhiReadBuffer` in a later pass.
