# mrhi-0013. Submission: a frame reaches its driver as one read-only view

Status: Accepted

## Context

A compiled, recorded frame (mrhi-0008, mrhi-0011) lives in the device's
private tables: its resources, passes, barriers and command chunks. A
driver translates it into Vulkan, D3D12, Metal or WebGPU work when it is
submitted, and must never meet a record it cannot translate. Device
objects can be destroyed while a frame is open, and their slots taken
again by new objects.

## Decision

- **The view:** submitting hands the driver one read-only view, built
  in tables the device allocated with itself:
  - the frame's resources, by frame slot: device textures and buffers
    by driver handle, transients by def with the usage their passes
    derive and their place in the frame's memory, and whether a kept
    pass uses each;
  - its kept passes in the order they run: class, label, targets with
    the stores the compile derived, render area, query sets' handles
    and timestamp queries, and their first command chunk;
  - its barriers in the order they run;
  - its command chunks;
  - its upload bytes, valid until the frame finishes;
  - the readback ring, which the driver fills before it reports the
    frame finished;
  - the bytes its transients take together.
- **Labels** are copied when a pass is added, into a table of
  `framePasses` labels.
- **Render areas** are measured when a pass is added, so a kept pass
  that is never begun still clears its targets at the right size.
- **Snapshots:** importing a device texture or buffer copies its def or
  size, driver handle and carried state into the frame. An object
  destroyed while the frame is open keeps its handle in the frame, and
  its final state is carried back only while it is still live, so an
  object that takes its slot is never touched.
- **Surface images:** `mrhiAcquireSurfaceImage`, while the frame is
  declared, answers `mrhi_success` or `mrhi_suboptimal` with an image,
  or `mrhi_occluded`, `mrhi_errorOutOfDate` or `mrhi_errorDeviceLost`
  without one, the same answer again for the rest of the frame. The
  image is a texture of the surface's configuration, tracked and never
  aliased; it begins undefined, so it is written before it is read, a
  pass that writes it is kept with its store, and it ends the frame in
  `mrhi_statePresent`. The driver presents every image of a submitted
  frame after its work; a dropped frame, one whose submission fails,
  and one open when its device is destroyed give their images back.
  A surface whose image the open frame holds is not configured again.
- **Handles in streams:** commands name pipelines, samplers and query
  sets by driver handle; a driver retires a destroyed object only once
  the next frame submitted after the destruction has finished.
- **The test driver** walks every submitted frame as a driver would:
  each resource, pass and barrier, and each command record, its payload
  inside its chunk, every frame resource it names of the right type,
  every handle one it made, and every upload and readback inside the
  staging and ring. It traps on anything else and reports what it
  walked in the log `mrhiTestAdapter.frameLog` names.

## Consequences

Drivers read one documented view and never the core's tables, so either
side can change its layout. Every frame a test submits is a
conformance check of the recorded streams without a GPU. The view's
tables cost the device about 60 bytes a resource and 550 a pass, and
the labels 256 bytes a pass.
