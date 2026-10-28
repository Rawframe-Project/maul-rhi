# Overview

How Maul RHI is put together, what its domain adds to the [family's rules](maul-conventions.md), and where each part is explained.

## The shape

```
program ── public API (include/maul-rhi/, generated from contract/mrhi.json)
              │
           the core (src/*.c): validation, ids, requests, the frame graph,
              │                 encoders, the shader container reader
           the driver SPI (src/driver.h): two vtables and one read-only frame view
              │
           [validation layer]  (MAUL_RHI_VALIDATION)
              │
           one native driver: vulkan_* │ d3d12_* │ metal_* │ webgpu_*
           or the test driver (driver_test.c) or a driver built outside the tree
```

The **core** owns every decision a program can observe: what is valid, which ids are live, how a frame is ordered, culled, barriered and placed in memory, and what each request answers. It is the same code on every platform, and almost all of it is tested on the test driver without a GPU.

A **driver** only translates. It reports adapters and their facts, makes objects when told, and at submission turns one read-only view of a frame into its API's commands. It never calls the core and never meets an unchecked call. [drivers.md](drivers.md) describes the interface; each API's traps are on its own page: [vulkan.md](vulkan.md), [d3d12.md](d3d12.md), [metal.md](metal.md), [webgpu.md](webgpu.md).

The **contract** (`contract/mrhi.json`) is the public API as data: the headers, the defaults and the capability tables are generated from it, and every concept in it records how each API provides it. [contract.md](contract.md) explains it.

## Where each part is explained

| Topic | Page |
|---|---|
| The contract and the versions | [contract.md](contract.md) |
| Requests, notifications and device loss | [requests.md](requests.md) |
| Memory, limits and allocators | [memory.md](memory.md) |
| The frame graph: declaring, compiling, submitting | [frame-graph.md](frame-graph.md) |
| Encoders, the command arena, uploads, readbacks, queries | [recording.md](recording.md) |
| Binding tables, the root block, heaps and sealing | [binding.md](binding.md) |
| Shader containers and pipelines | [shaders.md](shaders.md) |
| Surfaces and presentation | [surfaces.md](surfaces.md) |
| The driver SPI, the test driver, the validation layer | [drivers.md](drivers.md) |
| The conformance suite and the CTS cases | [conformance.md](conformance.md) |
| Words used throughout | [glossary.md](glossary.md) |

## The library's profile

The family asks each library to state what its domain adds to the common rules.

- **Determinism.** GPU output is exempt from the family's same-bits rule: drivers and hardware round, filter and blend differently, and the last bits of a pixel may differ between machines. Everything the library decides itself is deterministic: validation, the frame graph's pass order, culling, barriers and memory placement for the same declarations, and the answers to the same requests in the same order.
- **Threads.** The library starts none. Different passes of a frame may record on different threads at once; every other object is used by one thread at a time. Threads a GPU driver or the browser runs are the platform's, and no library code runs on them.
- **Calling back in.** The library calls only the program's allocator, and the allocator calls no function of the library. The instance and each device hold their allocator behind a guard that marks them busy while it runs, their drivers' allocations included. While it is set, every function that would change that instance or device refuses with `mrhi_errorBusy` and counts one misuse; `mrhiDestroyInstance` and `mrhiDestroyDevice` refuse the same way and destroy nothing. A call of one object that changes another holds that object busy too while host code runs: a device's configure, unconfigure and destruction hold its instance, whose surface slots, answers and device count they change, so the instance's functions and those of its devices that change it refuse meanwhile; and destroying a surface, which ends its configuration, refuses while the device it is configured on is busy. Calls that use only a device never mark its instance, which another thread may be using. Pass recording calls, which may run on other threads and never allocate, are not refused, nor are calls that only read.
- **Stale ids.** Ids outlive their objects by design: a program may destroy an object while a running frame still holds it, and a request may be answered after the object it named is gone. A stale id is therefore not misuse. Every call that takes one returns `mrhi_errorStale`, changes nothing and counts nothing, and `mrhiGetInstanceMisuse` and `mrhiGetDeviceMisuse` count only `mrhi_errorInvalid` refusals.
- **Memory.** An instance and each device are made with the caller's allocator and take all their memory then, sized by named limits. A device owns its objects and its GPU memory; [memory.md](memory.md) says which driver places what.
- **Platform dependencies.** The Vulkan loader, opened at run time, with the Khronos C headers kept as published in `khronos/`; `d3d12.dll` and `dxgi.dll`, opened at run time, with the DirectX headers kept as published in `directx/` and the Windows SDK's DXGI headers; the Metal, QuartzCore, CoreGraphics and Foundation frameworks on Apple systems; the browser's WebGPU through JavaScript compiled into the library. Window handles come in as opaque pointers; no window library is linked. The conformance suite links the XCB client library where it is installed, to make a window of its own.
- **Data formats.** The shader container is an asset format ([shader-container-format.md](../user/shader-container-format.md)). The pipeline cache envelope is the one state format, and it is not read across 1.y: it is read only under the ABI version that wrote it (`MRHI_ABI_VERSION`), refusing another with `mrhi_errorVersion`. Before 1.0 that is the whole version, so even a patch release refuses a cache; from 1.0 on it is major.minor, so a patch release keeps one. The library generates no data files.
- **Size.** The library with its WebGPU driver, at `-Oz` without its tests and with every public function kept, stays within 128 KiB of wasm; `tools/wasm_size.py` measures it in CI and fails past the budget. The web build is where size costs most, so the budget is checked on every change rather than at release.

## Source layout

Files are named by their part: `instance.c`, `device.c`, `frame.c`, `pass.c`, `plan.c` and the like are the core; `driver_<api>.c` is a driver's instance side and `<api>_*.c` (`.m` for Metal) the rest of it; `src/generated/` is written by the generators. The module graph in `tools/modules.txt` says which file may use which, and keeps every driver apart from the others and from the core's private tables. The headers installed for outside drivers are the SPI's: `driver.h`, `command.h`, `reflection.h`, `container.h`, `container_interface.h`.
