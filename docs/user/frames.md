# Resources and frames

All GPU work reaches a device as a frame. A frame declares its resources and passes up front; the library culls what nothing needs, plans the barriers and places transient memory, the program records each pass, and the driver translates the frame at submission. There are no barriers, states, fences or semaphores in the API, so a hazard cannot be written.

## Contents

- [Resources](#resources)
- [A frame, step by step](#a-frame-step-by-step)
- [Moving bytes](#moving-bytes)
- [Draws beyond the basics](#draws-beyond-the-basics)
- [Queries](#queries)

## Resources

**Device objects** outlive frames: buffers (`mrhiCreateBuffer`), textures (`mrhiCreateTexture`) and their views (`mrhiCreateView`), samplers and query sets. A texture's usages are fixed when it is made, and its view formats may name only its sRGB or linear twin. Destroying an object that a submitted frame still uses is safe: the driver keeps it until that frame has finished. A new buffer or texture reads as zeros: the first frame that imports it clears it whole, every mip, layer and aspect of a texture, in a pass of the library's own that takes nothing from the device's frame budgets, so the frame's first pass that uses it sees zeros; passes that do not use it may run beside the clear. A texture of one mip and layer whose first use is a target that clears it and keeps the result skips that clear, as the target writes all of it. A texture made `mrhi_textureTransient` is never cleared, as its contents live only inside a render pass, and the textures a frame declares follow the rule for transient resources below. That pass may appear in a frame's barriers and plans, and the program's passes never have its id.

**Transient resources** live for one frame: `mrhiDeclareBuffer` and `mrhiDeclareTexture` describe them, and the compile places them in the frame's memory, overlapping those whose uses do not overlap. A pass may read a transient only after an earlier pass of the frame wrote it, or `mrhiAddPass` refuses it with `mrhi_errorInvalid`. Only what a pass wrote is defined: after a write to part of a transient, the rest holds whatever its memory held, which differs by backend, so a pass writes all that a later pass reads. A render target that is only written and then discarded stays on chip on tile GPUs. A texture that lists view formats is never transient.

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
    if (mrhiDevice_BeginFrame(device, &frame) != mrhi_success)
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
            mrhiDevice_CompileFrame(device) == mrhi_success;

    const mrhiTextureCopy source = {.resource = target};
    const mrhiExtent3d extent = {64, 64, 1};
    bool recorded =
        built && mrhiPass_Begin(device, draw) == mrhi_success &&
        mrhiPass_SetGraphicsPipeline(device, draw, pipeline) == mrhi_success &&
        mrhiPass_SetRootBlock(device, draw, 0, color, 4 * sizeof(float)) == mrhi_success &&
        mrhiPass_Draw(device, draw, 3, 1, 0, 0) == mrhi_success &&
        mrhiPass_End(device, draw) == mrhi_success &&
        mrhiPass_Begin(device, reading) == mrhi_success &&
        mrhiPass_ReadTexture(device, reading, &source, &extent, pixelsOut) == mrhi_success &&
        mrhiPass_End(device, reading) == mrhi_success;
    if (!recorded)
    {
        // Nothing of the frame runs.
        (void)mrhiDevice_DropFrame(device);
        return false;
    }
    return mrhiDevice_SubmitFrame(device, tokenOut) == mrhi_success;
}
```

**Declaring.** A device has one open frame at a time. Each pass has a class (`mrhi_passGraphics`, `mrhi_passAsyncCompute` or `mrhi_passTransfer`), its render and depth targets with their load and store operations, and every other access it makes (`mrhiAccess`: a resource, how it is used, which mips and layers). Within a pass a part of a texture is used in read-only ways or in one writable way, as WebGPU's usage scopes require. Passes of one class run in the order they are added.

**Where passes run.** A class says where a pass may run beside the graphics work, and the driver decides. On D3D12, async compute and transfer passes run on a compute and a copy queue of their own, ordered after the passes they depend on, except where those queues cannot take the pass: a transfer pass that touches a texture or clears a buffer runs on the compute queue, as does the library's pass that zeroes new objects, unless it clears a new render target or depth texture, which takes the graphics queue, and an async compute or transfer pass that samples a texture, or that uses one a graphics pass left as a target or sampled, runs on the graphics queue in the frame's order. So async compute overlaps graphics work when its textures are storage or copy textures. On Metal, async compute and transfer passes run on command queues of their own, except a pass that touches a surface image, which runs on the graphics queue. Vulkan and WebGPU run every pass on one queue in the frame's order.

**Compiling.** `mrhiDevice_CompileFrame` culls passes whose results nothing reads, unless a pass sets `neverCull`; chooses each target's store, keeping it only when a later kept pass reads it or the texture is imported; plans the barriers per mip, layer and plane; and places the transients. `mrhiPass_IsKept`, `mrhiPass_GetPlan`, `mrhiResource_GetPlan` and `mrhiDevice_GetFrameBarriers` show the result, for tests and graph tools, and `mrhiDevice_GetFrameUsage` the memory the transients take.

**Recording.** Between `mrhiPass_Begin` and `mrhiPass_End`, the encoder calls record a kept pass: pipelines, bindings, the root block, dynamic state, draws, dispatches, copies, debug groups and queries. Passes may record on several threads at once, each pass on one. Every call is checked against WebGPU's rules and against the pass's declarations, on every driver: a binding, copy or draw that touches a resource the pass did not declare is refused. A kept pass that is never begun records nothing, but still loads and stores its targets.

**Submitting.** `mrhiDevice_SubmitFrame` returns a token that `mrhi_deviceFrameDone` answers in the device's queue. `mrhiRequest_WaitFrame` waits for it with a timeout where the platform allows blocking; on the web it never blocks. `mrhiDevice_WaitNotification` waits instead for the device's next record, whatever it answers: it takes in what the driver finished, then waits for the oldest running frame, and returns `mrhi_success` with the record left in the queue, `mrhi_timeout`, or `mrhi_empty` at once when nothing is owed. A readback is owed only once its frame is submitted: one recorded in the frame still open counts as nothing to wait for. A pending pipeline needs no wait on a native driver, which makes it at its creation and answers at the next poll. `mrhiDevice_DropFrame` abandons an open frame: nothing of it runs.

## Moving bytes

- **Uploads.** `mrhiPass_WriteBuffer` and `mrhiPass_WriteTexture` copy the program's bytes at the call into the frame's staging, which `frameUploadBytes` sizes for each frame in flight, and record a copy from it. The program's memory is free again when the call returns.
- **Readbacks.** `mrhiPass_ReadBuffer` and `mrhiPass_ReadTexture` copy into the device's readback ring (`readbackBytes`). Once the frame has finished, `mrhi_deviceReadbackReady` answers, and `mrhiRequest_TakeReadback` copies the bytes out, rows packed tightly. Take every readback: the ring frees in order from the oldest, so one left untaken holds the ring.
- **Clears.** `mrhiPass_ClearBuffer` zeros a range of a buffer, without staging, in a pass that declares it a copy destination: counters, indirect arguments and a culling pass's output start this way.
- **Copies.** `mrhiPass_CopyBuffer`, `mrhiPass_CopyBufferToTexture`, `mrhiPass_CopyTextureToBuffer` and `mrhiPass_CopyTexture` record in passes without targets, checked as WebGPU checks them. An empty copy, write or read records nothing; a read is still answered, with no bytes.

A full command arena, staging or ring refuses the call with `mrhi_errorCapacity` and marks its pass; the frame's submission is then refused too, so nothing is dropped silently. The device's diagnostics name the limit, and `mrhiDevice_GetFrameUsage` shows how much of each the frame took ([devices](devices.md#opening-a-device)). Raise the device limit and try again on the next frame.

## Draws beyond the basics

- **Indirect draws and dispatches** read their arguments from a buffer the pass declares with the indirect access. As in WebGPU, the arguments are read on the GPU and not checked: a draw with a nonzero first instance on a device without `indirectFirstInstance`, or a dispatch past the limit, does nothing.
- **Counted multi-draw** (`multiDrawIndirectCount`): `mrhiPass_DrawIndirectCount` and `mrhiPass_DrawIndexedIndirectCount` make up to `maxCount` draws from packed records, the number drawn read on the GPU from a count a culling pass wrote. A frame's `maxCount`s together stay within the device limit `frameIndirectDraws`.
- **Multiview** (`multiview`): a pass's `viewCount` renders it into that many consecutive layers of its targets, with pipelines made for as many views. Shader entries read the view index, so their containers carry no WGSL. The adapter limit `multiviewViews` bounds the views: four on Direct3D 12, often two on Metal.

## Queries

Query sets hold occlusion results, timestamps or pipeline statistics, at most 4096 queries each.

- **Occlusion:** a render pass names its occlusion set; `mrhiPass_BeginOcclusionQuery` and `mrhiPass_EndOcclusionQuery` bracket draws, one query at a time. A nonzero result means some sample passed.
- **Timestamps** (`timestampQuery`): a pass names its timestamp set and the queries written at its start and end. Values are ticks; `mrhiDevice_GetTimestampPeriod` gives nanoseconds per tick. Timestamps are written at pass boundaries only, the one place WebGPU allows, by a pass of any class. A pass that declares a query resolve access runs after every timed pass before it, and a timed pass after every such pass before it, whatever their classes and queues, so a resolve reads the times of the passes before it in the frame's order and zero for the others. A pass's end is never before its start, but two passes' times are not ordered against each other, even when one follows the other: a later pass may start, by its time, before an earlier one ends.
- **Pipeline statistics** (`pipelineStatisticsQuery`, Vulkan and Direct3D 12): `mrhiPass_BeginStatisticsQuery` and `mrhiPass_EndStatisticsQuery` bracket work in one graphics-class pass; each resolves to eleven 64-bit counters.

Occlusion and statistics queries are written, and every query is resolved, in graphics-class passes; timestamps are written by a pass of any class. Each query is written at most once a frame. `mrhiPass_ResolveQueries`, in a graphics pass without targets, writes 64-bit values into a buffer the pass declares with the query resolve access, at an offset that is a multiple of 256; a query the frame did not write resolves to zero. Read the values back with `mrhiPass_ReadBuffer` in a later pass.
