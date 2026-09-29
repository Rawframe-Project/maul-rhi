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
  - Acquiring a surface returns `success`, `suboptimal`,
    `out_of_date`, `occluded_skip` or `device_lost`, and surfaces
    present at submit.
- **Obligations:**
  - barriers synthesized per mip, layer and plane, from the states
    uses leave;
  - transients aliased by lifetime (pooled on WebGPU);
  - transient load and store operations derived.
- **The plan is readable** after the compile (`mrhiGetFrameBarriers`,
  `mrhiGetResourcePlan`), for conformance tests and graph tools.
- **Imported state:** an imported object carries one state between
  frames. Parts left in other states are unified at the frame's end.
- **One state per pass:** within a pass, a texture's part is in one
  state. Sampling a read-only depth target shares its state.

## Consequences

Hazards stay unrepresentable without a single callback, recording
scales across threads, and every piece of the graph's planning is
testable without a GPU.
