# Drivers

The driver SPI is the line between the core, which decides, and a driver, which translates. This page explains the interface's shape and the rules on each side, the test driver, the frame walk, the pipeline walk and the validation layer. Each API's own driver is on its page: [vulkan.md](vulkan.md), [d3d12.md](d3d12.md), [metal.md](metal.md), [webgpu.md](webgpu.md).

## The interface

A driver is an instance vtable and, for each device it opens, a device vtable, each beside the driver's own pointer (`mrhiInstanceDriverVtable`, `mrhiDeviceDriverVtable` in `src/driver.h`). The instance side searches for adapters, reports format and surface capabilities, makes surfaces and opens devices. The device side makes and destroys objects, configures surfaces, acquires images, answers memory questions for declared resources, submits frames and is polled.

The rules every driver keeps:

- **Everything it is given has been checked.** Defs, containers, pipelines and frames reach it only after the core has refused anything WebGPU would refuse and anything the device was not granted. A driver checks only what its API adds.
- **It never calls the core.** Long operations (adapter searches, device openings, pipelines, frames) start at the call and are answered by events the next `poll` returns, each with the tag the core gave. A driver that finishes synchronously answers at the next poll all the same, so every driver behaves alike.
- **Handles** are 64-bit and never zero. The core maps its ids onto them, and command streams name pipelines, samplers and query sets by handle.
- **Retirement.** An object the core destroys may still be named by a frame being recorded or one already submitted. A driver releases it only once the next frame submitted after the destruction has finished, or when the device ends.
- **A loss is reported once**, as an event or a returned `mrhi_errorDeviceLost`, and the core asks for nothing after it ([requests.md](requests.md#device-loss)).

Drivers are compiled into the library, since the web loads no code at run time and the family's libraries ship alone. One native driver is built per target: Metal on Apple systems, Direct3D 12 on Windows (or Vulkan), Vulkan elsewhere, WebGPU on the web. `src/driver.c` starts the one an instance asks for: a test driver or an outside driver chained on its def, else the build's own.

## The handshake

Each vtable opens with the SPI version and its size. A vtable of another `MRHI_SPI_VERSION` is `mrhi_errorVersion`; one smaller than the core knows, or missing a function, is `mrhi_errorInvalid`. A device vtable is checked when the device opens, and a refused device is destroyed through its first function, which is `destroy` in every version for exactly this reason. Any change to the installed SPI headers or to a public type they reach raises `MRHI_SPI_VERSION`, and so does any change to what a driver must do or may assume that leaves the headers as they are, such as a command record's meaning or the order of a frame view, so that a driver of the older meaning is refused at the handshake rather than misreading a frame. The conformance suite's mustpass list is renamed to the new version ([conformance.md](conformance.md#mustpass-lists)).

The installed SPI headers are `driver.h`, `command.h` (the command records), `reflection.h` (a shader's decoded reflection), `container.h` and `container_interface.h`. Their types, and the public types they reach, are the interface; the core functions they also declare are not part of it, and an outside driver must not call them.

## Recorded commands

A pass's commands are 32-byte records in the device's arena, each followed by its payload, linked chunk by chunk per pass (`command.h`; [recording.md](recording.md#the-arena)). A driver walks a pass's chunks in order at submission. Every record has been checked against the pass's declarations and WebGPU's rules, so a driver never meets one it cannot translate; a record it does not know is a bug in the core, not input to handle.

## The test driver

Built with `MAUL_RHI_TEST_DRIVER` and chosen by an `mrhiTestDriverDef` on the instance def, it has no GPU. It finds the adapters the test describes, answers every request at the next poll, finishes frames at once, and loses its devices when a flag the test owns is set. It exists so that every refusal and ordering rule of the core is tested on every platform in CI without a GPU, and so that programs can test their own use of the library the same way.

It traps (`MRHI_ASSERT`) when the core breaks the SPI: work asked of a lost device, a handle it never made, a frame view that does not hold together.

## The frame walk

`src/frame_walk.c` reads a submitted frame as a driver would: every resource, pass and barrier; each pass's waits, each naming an earlier pass of its class or none; every command record, its payload inside its chunk, every frame resource it names of the right type, every handle one the driver made; every upload and readback inside the staging and the ring. The test driver traps on any fault it finds and fills the `mrhiTestFrameLog` a test asked for. Every frame a test submits is therefore also a check of the recorded streams.

## The pipeline walk

`src/pipeline_walk.c` checks a pipeline the core hands a driver against the rules its shader's reflection decides: every vertex input fed by an attribute of its scalar class, the binding tables and vertex buffers within their joint limit, each written color target fed by an output with its channels, its scalar class and, when its color blend reads alpha, four components, a second blend source written when a target blends from one, no color targets without a fragment entry, no alpha to coverage beside a written sample mask, each fragment input matched by a vertex output, room for a point list's point size, and constant values that name the shader's constants once, fit their types and give every required one. The rules are written again from WebGPU's, not by calling the core's checks in `graphics.c` and `pipeline.c`, so a slip in one does not hide in the other. The test driver traps on any rule a pipeline breaks, so a test or fuzz input that gets a broken def past the core stops there. `test/test_pipeline_walk.c` breaks each rule once.

## The validation layer

The core checks every call on every build, so an API validation layer above it would repeat the core. What nothing else checks is the other side of the SPI: what a driver answers, which matters most for drivers built outside the tree, and whether the frames the core submits to a real driver hold together. `MAUL_RHI_VALIDATION` builds a layer between the core and whichever driver an instance starts, wrapping both vtables. It walks every submitted frame with the frame walk and checks the driver's answers: events only for requests made and within the room given, adapters with distinct nonzero handles, known kinds and names in well-formed UTF-8 inside their buffers, objects with nonzero handles, acquired images, power-of-two memory alignments, sample counts the API knows, loss reasons and surface colors the contract lists, presentable surface caps that meet the floors, loss messages within their room and in well-formed UTF-8, device vtables that pass the handshake.

Breaches are counted (`mrhiInstance_GetDriverFaultCount`) and recorded in the instance's diagnostic queue under driver codes, never printed or called back. The call goes on with the driver's answer, except that an event count past the room given is clamped. The core does not rely on the layer for anything it would read past, place wrong or pass on, and checks it itself on every build:

- An event count past the room the core gave is cut to the room, so the core never reads past its array.
- A pipeline's answer is taken only for a slot within the device's pipeline budget whose pipeline is still compiling under that request; any other is ignored, as an unknown frame token is.
- A pipeline cache size too large to hold with the envelope's header fails the export with `mrhi_errorPlatform`.
- A declared resource's memory alignment of zero, or one that is not a power of two, fails the compile with `mrhi_errorPlatform` instead of placing resources over each other; a resource of 0 bytes, kept on chip, is not placed and its alignment not read.
- A refused submission's budget is recorded only when it is one of the `mrhi_diagnosticSubmit` codes.
- An adapter kind the contract does not list becomes `mrhi_adapterUnknown`, and an adapter's name ends within `MRHI_ADAPTER_NAME_BYTES` before its first byte that is not well-formed UTF-8.

Of what a driver reports for a device's loss or a surface: a loss reason the contract does not list becomes `mrhi_lossUnknown`, a loss message ends within `MRHI_LOSS_MESSAGE_BYTES` before its first byte that is not well-formed UTF-8, so a character the room cuts is left out whole, and a surface color with an unknown format, primaries, transfer or range is dropped from the caps, an adapter left with none not presenting. Caps a driver reports as presentable without FIFO, opaque alpha, render target usage, or twin views or images read as an adapter that does not present, in the caps, the compatible-surface search and surface configuration alike. A kind is `int32_t`, so each of these checks also refuses a negative value.

The native interop headers reach the driver beneath the layer. Release and web builds carry no layer; the core's own checks above run in every build.

## Drivers built outside the tree

An `mrhiExternalDriverDef` on an instance def carries an outside driver's instance vtable and pointer; [external drivers](../user/external-drivers.md) is the guide for writing one. Chained struct types with bit 30 set (`MRHI_STRUCT_DRIVER_DEFINED`) belong to such drivers: an instance of one hands such a struct, as a surface's source, to its driver unexamined, and every other instance refuses it as an unknown critical struct. Without an outside driver to test, a Linux build hands the conformance suite its own Vulkan driver through the same path (`test/conformance_vulkan_external.c`), so the path runs in CI.
