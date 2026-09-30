# mrhi-0008. The frame graph: declare, compile, record, submit

Status: Accepted

## Context

The frame graph is the only way work reaches the GPU. Its barriers
and aliasing are contract obligations, and the public API has no
barriers, states, semaphores or fences. Production graphs (Frostbite,
Unreal, Granite, Filament) run setup and execute callbacks, which the
family forbids (record 0018), and AMD's Render Pipeline Shaders shows
that a compiled graph can instead be recorded by the program, on any
thread.

## Decision

- **Lifecycle:**
  1. A frame is begun on a device; one frame is open at a time.
  2. Its resources and passes are declared, each pass with every
     access it makes.
  3. The frame is compiled: ordered, culled, and its barriers and
     aliasing planned from the declarations alone. The compile reports
     culled passes, which are never recorded.
  4. The kept passes are recorded.
  5. The frame is submitted.

  Declaration order is execution order within an execution class. A
  frame may be dropped before submission.
- **Resources:** frame-local, generation-checked ids for:
  - transient resources the graph makes and aliases;
  - imported device resources, tracked and never aliased;
  - acquired surface images.

  History passes between frames through numbered slots.
- **Passes:**
  - an execution class (`graphics`, `async_compute`, `transfer`);
  - declared accesses of fixed kinds over subranges, where a
    subresource is used in read-only kinds or in one writable kind
    per pass (WebGPU's usage scopes);
  - render and depth targets with load, store, clear and resolve;
  - culling from the outputs, with a never-cull flag.
- **Recording:**
  - one encoder per kept pass, used by one thread at a time, passes in
    parallel;
  - library-owned streams from a bounded per-frame arena;
  - commands checked against their pass's declarations.
- **Submission:**
  - `mrhiSubmitFrame` returns a token that one
    `mrhi_deviceFrameDone` record answers.
  - `mrhiWaitFrame` waits with a deadline, and never blocks on the
    web.
  - `framesInFlight` refuses a new frame (`mrhi_errorCapacity`)
    rather than blocking.
  - Acquiring a surface (`mrhiAcquireSurfaceImage`) returns
    `mrhi_success`, `mrhi_suboptimal`, `mrhi_errorOutOfDate`,
    `mrhi_occluded` or `mrhi_errorDeviceLost`, and surfaces present at
    submit (mrhi-0013).
- **Obligations:**
  - barriers synthesized per mip, layer and plane, from the states
    uses leave;
  - transients aliased by lifetime (pooled on WebGPU);
  - transient load and store operations derived.
- **Memory:** declared resources are placed in one frame memory by
  lifetime, first fit in first-use order, with sizes and alignments
  the driver reports (none for a transient texture a tile GPU keeps on
  chip). A target's store is kept only when a later kept pass reads it
  or the texture is imported.
- **Aliasing** (amended when the D3D12 driver placed transients): the
  first use of a declared resource placed over memory that resources
  used earlier in the frame is marked on its barriers (`aliasing`), a
  buffer's first use gaining a barrier from the undefined state for
  it, so that drivers order it after those resources' uses: Vulkan
  waits for all commands' writes, D3D12 records an aliasing barrier.
- **The plan is readable** after the compile, for conformance tests
  and graph tools:
  - `mrhiGetFrameBarriers`;
  - `mrhiGetResourcePlan`;
  - `mrhiGetPassPlan`;
  - `mrhiGetFrameMemory`.
- **Imported state:** an imported object carries one state between
  frames. Parts left in other states are unified at the frame's end.
- **One state per pass:** within a pass, a texture's part is in one
  state. Sampling a read-only depth target shares its state.

## Consequences

Hazards stay unrepresentable without a single callback, recording
scales across threads, and every piece of the graph's planning is
testable without a GPU.
