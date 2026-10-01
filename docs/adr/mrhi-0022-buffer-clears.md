# mrhi-0022. Buffer clears to zero

Status: Accepted

## Context

Programs often need a buffer range at zero before a pass writes it:
atomic counters, indirect arguments, a culling pass's output. Without a
clear, the only way was an upload of zeros through the frame's staging,
which costs staging bytes and a copy of every byte. WebGPU has
`clearBuffer`, which writes zeros only; Vulkan and Metal fill buffers
with a value; Direct3D 12 has no buffer fill outside a clear of an
unordered access view, which needs descriptors and a usage a copy
destination need not have.

## Decision

- `mrhiClearBuffer(device, pass, resource, offset, size)` writes zeros
  into a range of a frame buffer, in a pass without targets that
  declares the buffer's copy destination access. Offset and size are
  multiples of 4; the size may be `MRHI_WHOLE_SIZE` for the rest of the
  buffer; the range lies inside it. An empty clear records nothing.
- Vulkan records `vkCmdFillBuffer`, Metal `fillBuffer:range:value:`,
  WebGPU `clearBuffer`. Direct3D 12 copies from a device-owned 64 KiB
  buffer of zeros (D3D12 zeroes committed resources), one copy per
  64 KiB.
- Zeros only, since WebGPU writes no other value.

## Consequences

Every Direct3D 12 device holds 64 KiB of zeros, which its query
resolves also copy from. On Vulkan a copy destination's barriers now
name the clear stage beside the copy stage. The CTS's `clearBuffer`
cases are among the reference cases (mrhi-0021).
