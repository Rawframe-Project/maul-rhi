# mrhi-0025. The validation layer wraps the driver SPI

Status: Accepted

## Context

The requirements ask for a validation layer that "wraps the same
interface", and for hazard validation in validating builds. Dawn and
wgpu validate every call in their front end and let their backends
trust it; NVRHI puts its checks in a layer around its device because
its core does not check. Maul RHI's core already checks every call on
every build, every use of a resource against the pass's declared
accesses included, so an API layer would repeat it. What nothing
checked is the other side of the driver SPI: what a driver answers,
which matters most for drivers built outside the tree (mrhi-0024), and
whether the frames the core submits to a real driver hold together.

## Decision

- API and hazard validation stay the core's, on every build. Shader
  reads out of bounds are left to the native GPU validators, which CI
  runs.
- `MAUL_RHI_VALIDATION` (a build option, off by default) builds a
  layer between the core and whichever driver an instance starts,
  wrapping its instance and device vtables. It walks every submitted
  frame with the test driver's walk (now shared), and checks the
  driver's answers: events only for requests made and within the room
  given, adapters with distinct nonzero handles and names inside their
  buffers, made objects with nonzero handles, acquired images,
  power-of-two memory alignments, sample counts the API knows, device
  vtables that pass the handshake.
- Breaches are counted, never printed or called back:
  `mrhiGetDriverFaults(instance)` returns the count of an instance and
  its devices, 0 in builds without the layer. The call goes on with the
  driver's answer, except that an event count past the room given is
  clamped, so the core never reads past its array.
- The native doors (`maul-rhi/vulkan.h`, `d3d12.h`, `metal.h`) reach
  the driver under the layer.
- The conformance suite's `validation.clean` case requires zero faults;
  CI's AddressSanitizer cell and the local gate build the layer in.

## Consequences

An outside driver's mistakes show as a count in its conformance run
instead of as corruption later. The layer adds a wrap per SPI call and
a walk per frame, and only to builds that ask for it; release and web
builds carry none of it.
