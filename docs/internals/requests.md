# Requests and device loss

Everything that finishes later is a request, answered by exactly one record in its owner's queue. This page explains why, how the queues stay bounded without losing an answer, and how a lost device still answers everything it owes.

## Why queues

The four APIs finish work in four shapes: WebGPU only through promises, Metal through completion handlers on its own threads, Vulkan and D3D12 through fences the host checks. The family forbids callbacks into the program (a completion that runs at an unsafe point leaves every caller to handle re-entrancy) and threads of the library's own. A queue the program drains serves all four: a native driver answers when the core polls it, and the browser settles its promises into a queue on the JavaScript side that the poll drains. Nothing blocks on the web, and a program returns to the page's event loop to see answers.

## Two queues

- **The instance's queue** (`mrhiNextInstanceNotification`) answers adapter searches (`mrhi_instanceAdaptersFound`) and device openings (`mrhi_instanceDeviceReady`).
- **A device's queue** (`mrhiNextDeviceNotification`) answers pipelines (`mrhi_devicePipelineReady`), frames (`mrhi_deviceFrameDone`), readbacks (`mrhi_deviceReadbackReady`) and the loss notice.

Taking from a queue first polls the driver for what it finished, a small batch at a time, and turns each driver event into its record. A driver never calls the core; the only way its answers arrive is this poll.

## Never losing an answer

Each queue is a fixed ring sized by its owner's `notifications` limit. A request is accepted only when the ring has room for its answer beside every answer already owed (`mrhiHasRoomForAnswer`): a search, an opening, a pipeline, a submission or a readback that would overfill it is refused with `mrhi_errorCapacity` at the call. An accepted request therefore always finds room. A readback reserves room for its own record and its frame's. A device also keeps one record free from its creation for the loss notice, so every device holds at least two notifications.

Request ids come from one counter per owner, skipping zero, so every answer's id is unique on its owner and a null id never names a request. Frames' tokens, pipelines and readbacks share the device's counter.

## Answers for objects that went away

- A device destroyed while it opens answers its opening with `mrhi_errorStale`.
- A pipeline destroyed while pending answers `mrhi_errorStale` at once; its driver never reports it, and whatever the driver finishes later is dropped.
- An adapter search whose compatible surface ends first answers `mrhi_errorStale`.
- An instance refuses to be destroyed while it has devices, counting the call as misuse, so no order of destruction frees what a device still uses.

## Device loss

A GPU can hang, be reset or removed, or lose its driver: Vulkan reports `VK_ERROR_DEVICE_LOST`, D3D12 a removal reason, Metal command buffer errors, WebGPU `GPUDevice.lost` with a reason and a message. None recovers a device in place.

A driver reports a loss as a poll event with `mrhi_errorDeviceLost`, or by returning `mrhi_errorDeviceLost` from any call. The core loses the device the first time it sees either and never asks the driver for work again; the test driver traps if it is asked. Losing answers everything owed, in a fixed order: the loss notice (with a null request id), then every running frame and its readbacks, then every pending pipeline. Afterwards every call that needs the GPU answers `mrhi_errorDeviceLost`, while destruction keeps working, so a program can always clean up.

The loss report is fixed-size so a program can keep it or send it with a crash report: the reason, the last frame submitted and the last finished, the frame and pass that were running, and the driver's message, cut to `MRHI_LOSS_MESSAGE_BYTES`. The core fills the frames; the driver fills the rest from what its API tells (`lossReport` in the SPI). Today Direct3D 12 maps its removal reason onto the reason, WebGPU passes the browser's message and Metal the failed command buffer's, and no driver yet reads the faulting frame and pass from Vulkan's device fault extension, DRED or Metal's encoder information.

`mrhiSimulateDeviceLoss` loses a ready device on any driver as a driver's report would, with the reason `mrhi_lossSimulated`. The driver is told nothing: its work finishes unseen and is destroyed with the device. This is what the conformance suite's `loss.simulated` case and programs' recovery tests use. The test driver can also lose its devices when a flag the test owns is set, reporting the reason the test chose, for the paths only a real loss takes.
