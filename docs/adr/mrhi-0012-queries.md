# mrhi-0012. Queries: sets on the device, written once a frame, resolved into buffers

Status: Accepted

## Context

Occlusion queries are on the floor and timestamps are an optional
feature (mrhi-0006). The four APIs keep queries in different shapes:
Vulkan in query pools that must be reset outside a render pass before a
query is written again, D3D12 in query heaps resolved into a buffer in
the copy destination state, Metal in a visibility result buffer and
counter sample buffers, and WebGPU in query sets resolved into a buffer
made for query resolves. WebGPU writes timestamps only at a pass's start
and end, and Metal on Apple GPUs samples them only at stage boundaries.
The library allocates nothing after a device is made, and passes
record on several threads (mrhi-0011).

## Decision

- **Query sets** are device objects: `mrhiCreateQuerySet` with
  `mrhiQuerySetDef` (a label, the type, occlusion or timestamp, and a
  count of 1 to 4096, WebGPU's bound) and `mrhiDestroyQuerySet`, whose
  id ends at once. Timestamp sets need `timestamp_query`. Two device
  limits bound them: `querySets` (16) and `queries` (4096 across all
  sets). Each query keeps a mark in a table the device allocates once,
  and a set takes a contiguous run of it, first fit.
- **Graphics passes only:** queries are written and resolved in passes
  of the graphics class, where declaration order is execution order
  (mrhi-0008), so a resolve sees what earlier passes wrote without query
  sets becoming frame resources. Copy queue timestamps, which D3D12
  keeps in a separate heap type, never arise.
- **Occlusion:** a render pass's def names an occlusion set;
  `mrhiBeginOcclusionQuery` and `mrhiEndOcclusionQuery` bracket draws,
  one query at a time, none open when the pass ends. A result other
  than 0 means some sample passed.
- **Timestamps:** a pass's def names a timestamp set and the queries
  written at its start and end, each optional. A culled pass writes
  none. Values are ticks; the device reports nanoseconds per tick.
- **Once a frame:** a query is written at most once in a frame, which
  is stricter than WebGPU's once a pass. A second write is invalid. A
  driver then resets each set once, before the frame's first use, as
  Vulkan requires. The check is one atomic exchange of the query's mark
  with the frame's serial, exact while passes record in parallel.
- **Resolving:** `mrhiResolveQueries` in a graphics pass without
  targets writes 64-bit values into a frame buffer the pass declares
  with the query resolve access, at an offset that is a multiple of
  256. A query not written earlier in the frame resolves to 0, which
  the driver ensures from the stream. The program reads values back
  with `mrhiReadBuffer` in a later pass.

## Consequences

Programs valid here are valid on WebGPU, and a Vulkan driver never
meets a query written twice between resets. The marks cost 8 bytes a
query, 32 KiB by default. Resolving into buffers serves readbacks and
GPU consumers alike, at the cost of a pass between the resolve and the
readback.
