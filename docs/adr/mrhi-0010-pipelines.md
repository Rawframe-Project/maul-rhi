# mrhi-0010. Pipelines: made asynchronously from a shared reflection

Status: Accepted

## Context

Every API compiles pipelines, and compiling takes long enough to stall a
frame: WebGPU offers `createComputePipelineAsync` and
`createRenderPipelineAsync`, Metal completion handlers, and Vulkan and
D3D12 drivers compile on the calling thread unless a program makes its
own. The library runs no threads of its own and takes no callbacks. A
pipeline's binding layout comes from its shader's reflection
(mrhi-0009), which a program may want to free once its pipelines are
made.

## Decision

- **Asynchronous creation:** `mrhiCreateComputePipeline` returns the
  pipeline and a request at once; the device's queue answers the
  request with `mrhi_devicePipelineReady` and the outcome. A pipeline
  is usable once that answer is `mrhi_success`.
- **One request space:** frames' tokens and pipelines' requests come
  from one counter per device, so every answer's request id is unique on
  its device. A creation reserves room for its answer, as a submission
  does, so an answer is never lost.
- **Destruction** of a pending pipeline answers it `mrhi_errorStale` at
  once; its driver never reports it.
- **Typed ids** for compute and graphics pipelines over one table and
  one `pipelines` limit; a slot knows its kind, so an id of the other
  kind is stale.
- **Shared reflection:** the reflection is one block with a reference
  count, held by its shader and each pipeline made from it. A pipeline
  keeps working after its shader is destroyed, without a copy.
- **Constants** are doubles, as in WebGPU, checked against each
  constant's type: a boolean 0 or 1, an integer within its type's
  range, or a finite float within 32-bit range. Every constant without
  a default is set, each at most once.
- **Checks:** everything the reflection allows to be checked is checked
  before the driver sees the pipeline, by WebGPU's rules for compute and
  render pipelines; a def that contradicts itself, the shader or those
  rules is invalid, and one the device cannot do is unsupported.
- **Graphics defs** carry every state of WebGPU's render pipeline
  descriptor. Vertex buffers and attributes are arrays with counts, so
  no fixed array caps an adapter's limits; depth and stencil state sits
  in the def with `mrhi_formatNone` for none, and a field for an aspect
  the format lacks must keep its default, so a forgotten format is
  refused rather than ignored. Sample counts are 1 or a power of two
  every target format takes on the device.
- **Caches:** `mrhiGetPipelineCache` writes the driver's compiled
  pipelines in an envelope: a magic, a version, the size, a SHA-256
  digest, and the library version, driver kind and adapter vendor and
  device ids. A device def may give one back. It is checked as hostile
  input, then by the driver, and never fails the device:
  `mrhiGetPipelineCacheOutcome` reports it taken, absent, stale (another
  library version, driver or adapter, or declined by the driver) or
  damaged. WebGPU has no cache and declines every one.

## Consequences

Programs start pipelines during loading and poll for them, as for
frames, with one mechanism on every API. A driver that compiles
synchronously answers at the next poll. Because a container does not
record which entry uses which constant, a container's required
constants are required by every pipeline made from it.
