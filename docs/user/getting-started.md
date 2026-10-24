# Getting started

Maul RHI puts one API over Vulkan, Direct3D 12, Metal and WebGPU. A program opens an instance, asks it for adapters, opens a device on one, and sends work to the device as frames. This page covers building the library, the rules every call follows, and opening a first device.

## Building and linking

Maul RHI needs CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows, `clang-cl` (Visual Studio's "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix <prefix>
```

From CMake:

```cmake
find_package(maul-rhi REQUIRED CONFIG)
target_link_libraries(app PRIVATE maul-rhi::maul-rhi)
```

or through pkg-config as `maul-rhi`. The public headers are C17 and C++17, so any C or C++ program can include them.

Each build has one native driver, chosen by the target:

| Target | Driver | Notes |
|---|---|---|
| Linux, the BSDs | Vulkan 1.3 | The loader is opened at run time; no Vulkan SDK is needed to build. |
| Android | Vulkan 1.3 | Built with the NDK; see [platforms](platforms.md#android). |
| Windows | Direct3D 12 | `MAUL_RHI_VULKAN_DRIVER=ON` with `MAUL_RHI_D3D12_DRIVER=OFF` takes Vulkan instead. |
| macOS, iOS | Metal | |
| The web | WebGPU | Emscripten, or Clang's `wasm32-wasi` without Emscripten; see [platforms](platforms.md#the-web). |

The build options a program may want:

| Option | Default | What it does |
|---|---|---|
| `MAUL_RHI_BUILD_SHARED` | off | A shared library instead of a static one. |
| `MAUL_RHI_TEST_DRIVER` | on with the tests | The test driver, which needs no GPU; see [testing your program](troubleshooting.md#testing-a-program-without-a-gpu). |
| `MAUL_RHI_VALIDATION` | off | The validation layer around the driver; see [troubleshooting](troubleshooting.md#the-validation-layer). |
| `MAUL_RHI_LABELS` | on | Debug labels for RenderDoc, PIX, Xcode and the browser's tools. Off, labels are still checked and then dropped, so a shipping build pays nothing for them. |

## Opening a device

With `maul-rhi/instance.h` and `maul-rhi/device.h` included:

```c
// Opens a device on the best adapter, software rasterizers allowed.
// The caller destroys what it got, the device first: *deviceOut when
// not NULL, then *instanceOut when not NULL.
static bool OpenDevice(mrhiInstance** instanceOut, mrhiDevice** deviceOut)
{
    *instanceOut = NULL;
    *deviceOut = NULL;
    mrhiInstanceDef instanceDef = mrhiDefaultInstanceDef();
    if (mrhiCreateInstance(&instanceDef, instanceOut) != mrhi_success)
    {
        return false;
    }
    mrhiInstance* instance = *instanceOut;

    // Natively each answer is queued by the time the call returns; on
    // the web it comes between the page's tasks, so a page looks again
    // on a later frame.
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId searched;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiRequestAdapters(instance, &request, &searched) != mrhi_success ||
        mrhiNextInstanceNotification(instance, &record) != mrhi_success ||
        record.outcome != mrhi_success ||
        mrhiGetAdapters(instance, &adapter, 1, &count) != mrhi_success || count == 0)
    {
        return false;
    }

    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    mrhiRequestId opened;
    if (mrhiCreateDevice(instance, &deviceDef, deviceOut, &opened) != mrhi_success)
    {
        return false;
    }
    // mrhi_instanceDeviceReady answers the opening.
    return mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
           record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success;
}
```

[Devices](devices.md) covers what an adapter offers and what a device may ask for; [frames](frames.md) shows how work reaches it.

## The rules every call follows

**Requests and notifications.** Whatever finishes later (an adapter search, a device opening, a pipeline, a frame, a readback) returns a request id at once and is answered by exactly one record in its owner's queue: the instance's (`mrhiNextInstanceNotification`) or the device's (`mrhiNextDeviceNotification`). The library starts no thread and never calls the program. A native driver's answers arrive when the program drains the queue; a browser's come between the page's tasks, so a web program drains again on a later frame instead of waiting.

**Results.** Every fallible function returns an `mrhiResult`. `mrhi_success` is zero. A few positive values are outcomes, not failures: `mrhi_empty` for a drained queue, `mrhi_suboptimal` and `mrhi_occluded` for surfaces, `mrhi_timeout`. The errors:

| Result | Meaning |
|---|---|
| `mrhi_errorInvalid` | An argument the call can never take. It also counts one misuse on the instance or device. |
| `mrhi_errorFormat` | The data is malformed or does not contain what was asked for: a damaged shader container, or an entry, constants or pipeline state its reflection refuses. Bad data is not a bug in the program, so it counts no misuse. |
| `mrhi_errorStale` | An id whose object is gone. |
| `mrhi_errorCapacity` | A limit is reached. |
| `mrhi_errorUnsupported` | The adapter or driver cannot do it. |
| `mrhi_errorState` | The object is in the wrong state for the call. |
| `mrhi_errorVersion` | The program was built against other headers, or a container or cache has another format version. |
| `mrhi_errorOutOfDate` | A surface must be configured again. |
| `mrhi_errorPlatform` | The platform refused, for a reason the library cannot name. |
| `mrhi_errorDeviceLost` | The device is lost; see [devices](devices.md#device-loss). |

`mrhiResultName` names any value. [Troubleshooting](troubleshooting.md) shows how to learn which check refused a call.

**Ids.** The instance and devices are pointers. Everything they own is an id with a generation: an id outlives its object harmlessly, and using it afterwards answers `mrhi_errorStale`.

**Defs.** Every creation takes a def. Start each from its `mrhiDefault...Def` function and change fields: the default sets a cookie, and a def without it is refused rather than read. `mrhiDefaultInstanceDef` is built into the program from the headers it includes and stamps the def with their ABI version, so an instance refuses a program built against another release with `mrhi_errorVersion` instead of reading a def of another layout. A def's `next` takes chained structs, which add to a call. A chained struct the library does not know is refused with `mrhi_errorUnsupported`, unless bit 31 of its type is set, which marks a hint an older library may skip.

**Memory.** An instance and each device take all their memory when they are made, through the def's allocator, from the limits the def names: `mrhiInstanceLimits` and `mrhiDeviceLimits` (objects, frame resources, passes, accesses, command, upload and readback bytes). A zeroed allocator means the C library's. Past a limit a call answers `mrhi_errorCapacity`; nothing grows later. A library that is handed a device reads its budgets with `mrhiGetDeviceOwnLimits`, as it reads the GPU's with `mrhiGetDeviceLimits`.

**Threads.** Each function's documentation says which object it uses. An object is used by one thread at a time, except that different passes of a frame may record on different threads at once.
