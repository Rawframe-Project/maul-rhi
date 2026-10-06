# mrhi-0029. Debug labels a build can compile out

Status: Accepted

## Context

The requirements ask for debug labels on objects, passes and regions,
so that RenderDoc, PIX, Xcode and the browser's tools show names, and
that they "compile to nothing when disabled". Every build named
objects in its driver, copied pass labels into the frame and recorded
debug groups and markers (mrhi-0011), costs a shipped program pays
with no tool attached. Engines strip these at build time: Unreal's GPU
events behind build switches, bgfx's debug annotations behind a config
macro, PIX markers behind `USE_PIX`.

## Decision

- **`MAUL_RHI_LABELS`**, a CMake option, on by default. Off, the
  library still checks every label as before, so a call refused in one
  build is refused in every build, and then drops it: defs reach the
  driver without their labels, passes keep none, the frame allocates
  no label storage, and pushes, pops and markers record nothing.
- **Same results.** Group balance is still counted, so an unbalanced
  pass is refused alike; everything else a frame does is unchanged.
- The drivers are untouched: a def without a label is one they already
  name nothing for.
- One CI cell (`linux-clang-20`) builds without labels and runs every
  test and sample.

## Consequences

A shipping build pays for UTF-8 checks only; a debugging build names
everything. A program sees no difference in what it may call.
