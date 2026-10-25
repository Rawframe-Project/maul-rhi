# Platforms

What each platform's driver needs at run time, and what differs between them. Every driver grants the same floor; [devices](devices.md#features-limits-and-formats) covers what lies above it.

## Linux and the BSDs

The Vulkan driver opens the loader at run time (`libvulkan.so.1`) and lists a physical device that reports Vulkan 1.3 with a queue family that does graphics and compute, dynamic rendering, synchronization2, timeline semaphores, buffer device addresses and descriptor indexing, and the few Vulkan 1.0 features the floor uses (cube arrays, independent blending, anisotropy among them). Without a loader, or without such a device, the instance has no adapters. No Vulkan SDK is needed to build: the Khronos headers are part of the source.

Surfaces come from XCB or Wayland. Software rasterizers such as lavapipe are listed when a request allows software adapters.

## Windows

The Direct3D 12 driver opens `d3d12.dll` and `dxgi.dll` from the system directory and lists every adapter that opens a feature level 12_0 device with shader model 6.0, WARP among them as a software adapter. It uses whichever D3D12 runtime the process has: the system's, or the Agility SDK the program's executable selects through its `D3D12SDKVersion` and `D3D12SDKPath` exports. The library never selects one itself.

Surfaces are Win32 windows, presented through flip-model swapchains. HDR10 is offered where the window's monitor is in HDR mode, and immediate presentation where DXGI allows tearing.

A Windows build may take the Vulkan driver instead (`-DMAUL_RHI_VULKAN_DRIVER=ON -DMAUL_RHI_D3D12_DRIVER=OFF`).

## macOS and iOS

The Metal driver lists every Metal device that meets the floor, which needs cube array textures: every Mac's GPU, and Apple GPUs from the A11 on. It uses Metal's classic command queues and encoders, which every Metal device runs. Builds for iOS target iOS 15 and use what later versions add where they have it: BC compression from iOS 16.4, extended-range surfaces from iOS 16. Immediate presentation is macOS's alone. The iOS simulator's GPU has no cube arrays, so it lists no adapter.

MoltenVK and similar layers can run the Vulkan driver on Apple systems in a build that chooses it, but are neither tested nor promised.

## Android

The Vulkan driver runs on Android as on Linux, through `libvulkan.so`, and needs a device and system loader of Vulkan 1.3. Build with the NDK, r28 or newer:

```sh
ANDROID_NDK_HOME=<ndk> cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/android-emulator.cmake
```

The toolchain file defaults to x86_64 and Android 11 (API 30); `ANDROID_ABI` and `ANDROID_PLATFORM` change them. Surfaces come from an `ANativeWindow`. Android's swapchains reach sRGB through the images' own format (`twinImages`), and take any size within the surface's bounds, which the compositor scales to the window.

## The web

The WebGPU driver calls the browser's WebGPU through JavaScript compiled into the library, and needs a browser whose WebGPU has immediates, which carry the root block. Every request is answered by a promise the browser settles between the page's tasks, so a web program returns to the page's event loop and drains its queues on a later frame; `mrhiWaitFrame` and `mrhiWaitDeviceNotification` never block there. `mrhiWaitDeviceNotification` returns `mrhi_success` if the browser has already settled an answer, and otherwise `mrhi_timeout` at once, whatever its timeout: a pipeline's promise or a frame's completion settles only once the page yields, so waiting in place could never see it.

**With Emscripten,** link the library as any other: the JavaScript comes along in its objects.

**Without Emscripten,** with Clang's `wasm32-wasi` and wasi-libc:

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/wasm32-wasi.cmake -DCMAKE_C_COMPILER=clang-20
```

The build writes `maul-rhi.mjs` beside the library (installed to `share/maul-rhi/`, and named by the library target's `MAUL_RHI_WEB_GLUE` property). Its one export, `maulRhiImports(exports, module)`, returns the functions the page puts among the instance's `env` imports. `exports` is a function returning the instance's exports, which exist only once the imports are given; the program links with `--export-table`. `module`, when given, is the object the functions keep their state in, as Emscripten's `Module` would be.

Canvases are named by a CSS selector, or by a name registered in `Module.mrhiCanvases`, which lets the driver run in a worker on an `OffscreenCanvas` ([surfaces](surfaces.md#the-web-on-a-worker)). The browser places memory itself, so a frame's reported memory is an estimate there.

## Drivers built outside the library

Platforms whose GPU drivers cannot be public, such as consoles, are served by a driver built outside this repository against the installed driver interface. [External drivers](external-drivers.md) describes how.
