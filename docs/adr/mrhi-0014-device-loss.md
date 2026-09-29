# mrhi-0014. Device loss: terminal, answered, reported

Status: Accepted

## Context

A GPU can hang, be reset, be removed, or lose its driver while a
program runs. Vulkan reports `VK_ERROR_DEVICE_LOST`, D3D12 a device
removed reason, Metal command buffer errors, and WebGPU resolves
`GPUDevice.lost` with a reason and a message. None recovers a device
in place. The library must never end the process, must answer every
request exactly once (mrhi-0003), and must leave the program a bounded
account of what happened.

## Decision

- **Detection:** a driver reports a loss as a poll event with tag 0 and
  `mrhi_errorDeviceLost`, or by returning `mrhi_errorDeviceLost` from
  any call. The core loses the device the first time it sees either,
  and never asks the driver for work again.
- **Losing** is terminal: `mrhiGetDeviceState` answers
  `mrhi_deviceLost`, and the core answers everything it owes with
  `mrhi_errorDeviceLost`, in order: a notice (`mrhi_deviceLostNotice`,
  with a null request id), then every running frame and its readbacks,
  then every pending pipeline. The notice's record is kept free from the
  device's creation, so a device holds at least 2 notifications.
- **Afterwards**, every call that needs the GPU answers
  `mrhi_errorDeviceLost`: making objects, beginning and acquiring;
  submitting drops the open frame. Destroying objects and the device
  still works, and a readback answered lost is taken with that outcome.
  The program makes a new device.
- **The report:** `mrhiGetDeviceLossReport` gives a fixed-size
  `mrhiDeviceLossReport`: the reason (`mrhi_lossUnknown`,
  `mrhi_lossHung`, `mrhi_lossReset`, `mrhi_lossRemoved`,
  `mrhi_lossDriverFault`), the last frame submitted and the last
  finished, the frame and pass the GPU was running when the API tells
  (Vulkan's device fault, D3D12's DRED, Metal's encoder information),
  and the driver's message of at most `MRHI_LOSS_MESSAGE_BYTES`.
- **Injection:** the test driver loses its devices when the flag
  `mrhiTestAdapter.loseDevice` points at is set, reporting
  `mrhiTestAdapter.lossReason`, for the conformance suite's device-loss
  category. It traps if the core asks it for work after it said the
  device was lost.

## Consequences

A loss is one more outcome in the queue a program already reads, and
every request still completes once. The report is small enough to keep
or send with a crash report. Recovery is the program's: a new device,
and its objects made again.
