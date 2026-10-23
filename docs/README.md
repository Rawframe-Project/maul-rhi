# Maul RHI documentation

## Using the library

- [Getting started](user/getting-started.md): building, linking, the rules every call follows, a first device.
- [Adapters and devices](user/devices.md): features, limits and formats, opening a device, device loss.
- [Resources and frames](user/frames.md): buffers and textures, the frame graph, uploads and readbacks, queries.
- [Shaders, pipelines and binding](user/shaders.md): cooking containers, pipelines and caches, binding tables, bindless heaps.
- [Shader container format](user/shader-container-format.md): the byte layout, for cooks of other kinds.
- [Surfaces and presenting](user/surfaces.md): windows, presentation, Maul Window, workers on the web.
- [Platforms](user/platforms.md): what each platform's driver needs.
- [Native interop](user/native-interop.md): OpenXR and vendor upscalers.
- [Drivers built outside the library](user/external-drivers.md): consoles and other closed APIs.
- [Troubleshooting](user/troubleshooting.md): diagnostics, the validation layer, labels, testing without a GPU.

The [documentation site](https://rawframe-project.github.io/maul-rhi/) adds the API reference, generated from the headers, and the table of how every concept maps onto Vulkan, Direct3D 12, Metal and WebGPU.

## How it works

- [Overview](internals/overview.md) and [glossary](internals/glossary.md).
- [The contract](internals/contract.md), [requests and device loss](internals/requests.md), [memory](internals/memory.md).
- [The frame graph](internals/frame-graph.md), [recording](internals/recording.md), [binding](internals/binding.md), [shader containers and pipelines](internals/shaders.md), [surfaces](internals/surfaces.md).
- [Drivers](internals/drivers.md): the SPI, the test driver, the validation layer; and each driver: [Vulkan](internals/vulkan.md), [Direct3D 12](internals/d3d12.md), [Metal](internals/metal.md), [WebGPU](internals/webgpu.md).
- [Conformance](internals/conformance.md): the conformance suite and the WebGPU CTS cases.
- [Maul conventions](internals/maul-conventions.md): the rules every Maul library follows.

## Working on it

- [Development](operations/development.md): toolchains, platform runs, generated and vendored files.
- [Checks](operations/checks.md): the check tiers, the rules and what the test presets expect.
- [Debugging](operations/debugging.md): validation layers, sanitizers, frames that look wrong, fuzzing.
- [Releasing](operations/release.md).
