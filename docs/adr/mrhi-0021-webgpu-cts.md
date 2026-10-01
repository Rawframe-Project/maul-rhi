# mrhi-0021. The WebGPU CTS as the encoder's reference cases

Status: Accepted

## Context

The core refuses, on every driver, what breaks WebGPU's rules, so that
a frame that records anywhere records on the web. The WebGPU
conformance test suite (CTS) states those rules as small tables of
calls and their outcomes, maintained with the specification. The CTS
itself tests a WebGPU implementation, the browser's, which Maul RHI
only uses.

## Decision

- The CTS is not run against the library.
- Its validation cases whose calls have Maul RHI counterparts become a
  table-driven test on the test driver (`test/test_cts.c`), each naming
  its CTS test, the call it becomes and the outcome the CTS expects:
  success, or the library's refusal. A case the library decides
  otherwise is listed in the test with the reason.
- The CTS revision is pinned in the test, whose header carries the
  CTS's BSD-3-Clause notice for the translated cases.
- Cases come in slices: copies, then draws and dispatches, then render
  pass descriptors and queries.

## Consequences

The first slice found a real difference: WebGPU allows empty copies,
and the core recorded them, which Vulkan refuses
(`VUID-VkBufferCopy-size-01988`). Empty copies, writes and reads now
record no command; a read is still answered, with no bytes.

The cases Maul RHI decides otherwise are three rules, each kept:

- A layout's `bytesPerRow` or `rowsPerImage` of 0 is absent, where
  WebGPU's 0 is a value; the web driver passes it as undefined. So an
  empty copy of several layers that gives 0 is refused, and a
  `rowsPerImage` of 0 for one layer is taken.
- An index range is whole indices; WebGPU takes any bytes.
- A query is written at most once a frame (mrhi-0012); WebGPU allows
  once a pass.

Cases needing data a buffer cannot have (a size not a multiple of 4,
where that changes the outcome) are counted, not run. Both counts are
pinned in the test, so a change of either is seen.
