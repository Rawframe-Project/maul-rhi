# The frame graph

The frame graph is the only way work reaches the GPU, and the reason the public API has no barriers, states, fences or semaphores. This page covers how a frame is declared, compiled and handed to its driver; [recording.md](recording.md) covers what happens between the compile and the submission.

## Why declare first

Production frame graphs (Frostbite, Unreal, Granite, Filament) run a setup callback and an execute callback per pass. The family forbids callbacks into the program, and AMD's Render Pipeline Shaders shows the alternative: once the graph is compiled, the program can record it itself, pass by pass, on any thread. So a frame is five steps: begin, declare, compile, record, submit. Everything the graph decides comes from the declarations alone, before a single command exists, which also makes the whole of its planning testable on the test driver.

## Declaring

`mrhiDevice_BeginFrame` opens the device's one frame. Its resources are frame-local, generation-checked ids for three kinds of thing:

- **transients** the frame makes, places and may alias (`mrhiDeclareBuffer`, `mrhiDeclareTexture`);
- **imported** device objects (`mrhiImportBuffer`, `mrhiImportTexture`), tracked and never aliased, each carrying one state between frames;
- **acquired surface images** (`mrhiAcquireSurfaceImage`), tracked and never aliased.

Importing copies the object's def or size, driver handle and carried state into the frame, so an object destroyed while the frame is open keeps its handle there. Its final state is carried back only while it is still live, so an object that takes its slot is never touched.

A pass (`mrhiAddPass`) has a class, its render and depth targets with load, store, clear and resolve, at most one heap, its query sets, and the accesses it makes over subranges of mips, layers and planes. Within a pass a part of a texture is used in read-only kinds or in one writable kind, as WebGPU's usage scopes require, so each part has exactly one state per pass; sampling a read-only depth target shares the depth read state. Render areas are measured and labels copied when the pass is added, so a kept pass that is never begun still clears its targets at the right size.

## New buffers and textures

A device buffer or texture reads as zeros until the frame graph writes it, on every driver. WebGPU promises that itself, and Metal for buffers, but Vulkan memory and Direct3D 12 placed resources hold whatever their memory held, a committed resource is zeroed only in practice, and Metal leaves a new texture's contents undefined. So the core clears each new object in a pass of its own, the frame's first, a transfer pass labeled `maul-rhi: zero new objects` (`src/compile.c`).

An object is fresh from `mrhiCreateBuffer` or `mrhiCreateTexture` until a frame that imports it is submitted. An adopted image is never fresh, as its owner made its contents, nor is a texture made `mrhi_textureTransient`, whose contents live only inside a render pass. A fresh object's first import in a frame gives that pass a whole copy-destination use, every mip, layer and plane of a texture, in a region of uses past the program's `frameAccesses` that `frameResources` sizes. The pass is culled like any other, so a frame that imports no fresh object runs without it, and kept when it has uses, since it writes imported resources. The compile's last step records a clear for each use (`mrhiCommandClearBuffer` or `mrhiCommandClearTexture`), from chunks past the program's `frameCommandBytes`. The clear leaves an object in the copy destination state, so its first program use, in that frame or a later one when nothing else used it, may take a barrier it would not have taken; a texture's clear also takes one from the undefined state. Those barriers come from a reserve of `frameResources` barriers past the program's `frameBarriers`, one per object at most, since every barrier of the library's pass moves only the object it clears. So the program keeps every budget it asked for, and no more. A submitted frame ends a slot's freshness only while the slot holds the same object: one destroyed in its open frame may have given the slot to a new one, which still needs its own clear. A dropped frame clears nothing.

A texture of one mip and one layer, not a volume, whose first use in the frame is a color target that clears and keeps it, or a depth target, not read-only, that clears and keeps depth and any stencil, is left out of the clear when the frame compiles: that use writes all of it. No other use counts, since a draw or a copy covers only what it reaches. The texture still stops being fresh when the frame is submitted.

The program's passes take ids from 2. `mrhiPass_Begin`, `mrhiPass_IsKept` and `mrhiPass_GetPlan` answer `mrhi_errorStale` for the library's pass, which barriers, waits and resource plans may still name. Every driver buffer is made with the copy destination usage the clear needs; how each driver clears a texture, and what usage that adds, is on its page.

## Compiling

`mrhiDevice_CompileFrame` does four things, in `src/compile.c` and `src/plan.c`:

1. **Culling**, from the last pass back, at the granularity of whole resources: a pass is kept when it sets `neverCull`, or writes an imported resource, a surface image or a resource a kept later pass reads. Culled passes are never recorded and write no timestamps.
2. **Stores.** A target's store is kept only when a later kept pass reads it or the texture outlives the frame; otherwise it is discarded, which lets tile GPUs keep it on chip.
3. **Barriers.** Each resource's parts are tracked as disjoint boxes of mips, layers and planes mapped to the state their last use left them in. The uses of a resource bound at most 2c + 1 runs of its mips and of its layers for c uses, so the planner's room holds that many boxes of each, within the most mips and layers the device's limits allow a texture, times two planes; a frame within `frameAccesses` never runs out of it ([#320](https://github.com/Rawframe-Project/maul-rhi/issues/320)). Each kept use makes barriers from the states it meets: a texture leaving the undefined state, any change of state, or a write after a write. Two reads in one state need none. A buffer read in a second state takes one, as a texture does: the barrier into the first state made the last write visible to that state's reads only, and a later write must wait for the reads of both ([#70](https://github.com/Rawframe-Project/maul-rhi/issues/70)). The barriers are then put in the order they run.
4. **Placement** of transients in the frame's memory, first fit in first-use order ([memory.md](memory.md#a-frames-memory)).

Declaration order is execution order within a class. Across classes, the compile finds for each kept pass the latest pass of each other class it must follow:

- the passes whose uses of a part race its own, either writing or changing a texture's state;
- for a part it reads without a barrier, the pass whose barrier or write set the part's state;
- every class's last pass over memory that a placed resource takes from earlier ones;
- every class's passes before a native pass, and the native pass for every class's passes after it, since the program's commands may reach past what it declares;
- every class's timed passes before a pass that declares a query resolve access, and every such pass for a timed pass after it, since the resolve itself is recorded after the compile ([recording.md](recording.md#queries)).

Planning tracks this per part beside the state, so parts that never meet order nothing. The class limits what a pass may do (a transfer pass only copies; vertex, index and query resolve accesses are graphics only), so a driver can move async compute and transfer passes onto queues of their own without a change to the API, ordering each pass after the passes it must follow. The D3D12 driver runs async compute and transfer passes on a compute and a copy queue where those queues can record them ([d3d12.md](d3d12.md#queues)), and the Metal driver on command queues of their own ([metal.md](metal.md#queues)); the Vulkan and WebGPU drivers run every pass on one queue in declaration order, which keeps those orders as well.

## Aliasing

When the first use of a transient lands on memory that resources used earlier in the frame, its barriers are marked `aliasing`, and a buffer's first use gains a barrier from the undefined state, which it would not otherwise have. Drivers order such a barrier after the earlier resources' uses: Vulkan waits for all commands' writes, Direct3D 12 records an aliasing barrier and discards a placed render target or depth texture whole at its first use, as D3D12 requires before anything else touches it.

## Imported, sealed and presented states

- An imported object ends the frame in one state. Parts its passes left in different states are unified at the frame's end, so the next frame starts from a single known state.
- A surface image begins undefined and ends in `mrhi_statePresent`; a pass that writes it is kept with its store.
- An image adopted from OpenXR ends every frame in its color or depth target state, whatever its last use, since the runtime takes it back that way.
- A sealed resource ends the frame in `mrhi_stateSealed`, a read-only state any pass may read through a heap without declaring it ([binding.md](binding.md#sealing)).

## The readable plan

After the compile, `mrhiPass_IsKept`, `mrhiPass_GetPlan`, `mrhiResource_GetPlan` and `mrhiDevice_GetFrameBarriers` expose the plan, and `mrhiDevice_GetFrameUsage` its memory. They exist for conformance tests and graph tools, and they are why the hazards and aliasing cases of the conformance suite can be checked exactly on any driver.

## Submission

`mrhiDevice_SubmitFrame` hands the driver one read-only view, built in tables the device allocated with itself (`mrhiDriverFrame` in `src/driver.h`):

- the frame's resources by slot: device objects by driver handle, transients by def with the usages their passes derive and their place in frame memory, and whether a kept pass uses each;
- the kept passes in the order they run, with their class, label, targets and stores, render area, query sets, the resources they declare with the states they leave, their first command chunk, and the latest pass of each other class they must follow;
- the barriers in the order they run;
- the command chunks, the upload bytes, and the readback ring the driver fills before it reports the frame finished;
- the bytes the transients take together.

Drivers read this view and never the core's tables, so either side can change its layout. The view's tables cost a device about 60 bytes a resource and 630 a pass, and the labels 256 bytes a pass. The test driver and the validation layer walk each view completely ([drivers.md](drivers.md#the-frame-walk)).

Submission is refused while a pass still records, or when a pass ran out of arena, staging or ring. `framesInFlight` bounds the frames submitted and not yet finished: beginning another is refused with `mrhi_errorCapacity` rather than blocking, so the browser's event loop is never stalled. A dropped frame, or one whose submission fails, gives back its readbacks and its acquired images.
