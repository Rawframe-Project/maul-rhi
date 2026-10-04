# mrhi-0023. Pipeline statistics queries

Status: Accepted

## Context

The `pipelineStatisticsQuery` feature existed from the start, with no
query type to read it; the Vulkan driver granted it all the same, so a
program could be granted a feature it could not use. Vulkan and
Direct3D 12 both count the work between a begin and an end:
`VK_QUERY_TYPE_PIPELINE_STATISTICS` with the counters chosen at pool
creation, and `D3D12_QUERY_TYPE_PIPELINE_STATISTICS`, which always
writes eleven 64-bit counters. Vulkan's bit order and Direct3D 12's
struct order are the same eleven counters. Metal has a statistic
counter set only on some Macs, sampled at encoder boundaries, and
WebGPU has none.

## Decision

- A query set of type `mrhi_queryPipelineStatistics` needs the
  `pipelineStatisticsQuery` feature. Vulkan and Direct3D 12 grant it;
  Metal and WebGPU do not.
- `mrhiBeginStatisticsQuery(device, pass, set, query)` and
  `mrhiEndStatisticsQuery(device, pass)` bracket work in one pass of the
  graphics class, render or compute, rendering one view; one
  statistics query is open in a pass at a time, beside an occlusion
  query if any, and each query is written at most once a frame. The
  set is named at the begin, not on the pass def, since neither driver
  that grants the feature needs it when the pass begins.
- `mrhiResolveQueries` writes 88 bytes per statistics query: input
  assembly vertices and primitives, vertex invocations, geometry
  invocations and primitives, clipping invocations and primitives,
  fragment invocations, tessellation control patches and evaluation
  invocations, compute invocations. The geometry and tessellation
  counters are 0, since Maul RHI has neither stage. A query not written
  in the frame reads eleven zeros.

## Consequences

Both drivers resolve straight into the program's buffer, with no
scratch and no repacking, at 32 bytes per query of counters that are
always 0. Async compute passes take no statistics queries, since Vulkan
asks a graphics-capable command pool of a query counting graphics
stages; nor do multiview passes, over whose views Vulkan would spread a
query. Direct3D 12 resolves of unwritten queries copy its 64 KiB of
zeros (mrhi-0022) in pieces.
