# mrhi-0006. Features and limits, each mapped onto the four APIs

Status: Accepted

## Context

No concept enters the contract without a reviewed mapping onto Vulkan,
D3D12, Metal and WebGPU, each classed direct, emulated, restricted or
absent-rejected. The browser guarantees WebGPU's default limits on
every adapter it exposes.

## Decision

- **The floor:** compute with storage buffers, storage textures and
  atomics, indirect draws and dispatches, instancing, 2D, 2D array,
  cube, cube array and 3D textures, independent blend, and occlusion
  queries where nonzero means visible. It is granted without a request.
- **Optional features** (`mrhiFeatures`) are requested. One is granted
  only where the driver's API row is not absent-rejected and the
  adapter reports it; the core masks the rest off.
- **The limits** (`mrhiLimits`) take WebGPU's limits and defaults,
  named for the contract's binding model, with the heap sizes, the root
  block's bytes and the frames in flight added. `mrhiDefaultLimits` is
  the floor, and an adapter below it is not listed. Alignments are
  better lower; every other limit is better higher.
- **Timestamps** are written at pass boundaries only, the one form
  WebGPU allows.

Every row is in `docs/contract/mappings.md`, generated from the
contract.

## Consequences

A program learns at device creation what each driver can give, and no
driver claims what its API cannot do.
