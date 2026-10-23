# Drivers built outside the library

A platform whose GPU API cannot be public, such as a console, gets a driver of its own, built outside this repository against an installed Maul RHI and linked into the program. No file of the library is replaced, and the program's API does not change.

## The driver interface

An install of Maul RHI carries the driver interface's headers under `maul-rhi/spi/`: `driver.h`, `command.h`, `reflection.h`, `container.h` and `container_interface.h`. Their types are the interface (the SPI); the core functions they also declare are not part of it. [Drivers](../internals/drivers.md) explains how the core and a driver divide the work.

A driver is two tables of functions: an instance table and, for each device it opens, a device table, each beside the driver's own pointer. Each table opens with `MRHI_SPI_VERSION` and its own size. The driver reports finished work only when the core polls it and never calls the core, and it reads each submitted frame through one read-only view of the frame's resources, passes, barriers and recorded commands.

## Handing it to an instance

The driver's code makes its instance table before the instance and chains it on the instance def in an `mrhiExternalDriverDef` (`mrhi_structExternalDriver`), with its own pointer. The instance then starts that driver instead of the build's own; it owns the driver from a successful create, destroying it at its end, and leaves it to the program when creation fails. Its adapters report the driver `mrhi_driverExternal`.

The handshake is typed: a table of another `MRHI_SPI_VERSION` is `mrhi_errorVersion`; one smaller than the core knows, or missing a function, is `mrhi_errorInvalid`. A device table is checked when the device opens, and a refused device is destroyed through its first function, which stays `destroy` in every version. The external driver is refused beside the test driver or the Vulkan interop structs.

**Surfaces.** Chained struct types with bit 30 set (`MRHI_STRUCT_DRIVER_DEFINED`) belong to outside drivers. An instance of such a driver takes one as a surface's source and hands it to the driver unexamined; every other instance refuses it as an unknown critical struct. A console's window system needs no change to the library.

**Shader code.** Containers carry code for Vulkan, Direct3D 12, Metal and WebGPU only. A driver of another API needs its own code in containers, a new section type that readers already skip; its layout is settled with the first such driver.

## Admission

A driver is admitted on a run of the conformance suite that passes every case of its SPI version's list, `conformance/mustpass/spi-<N>.txt`. Build the suite against the driver by naming a CMake target that defines

<!-- guide: not run: declarations a driver's own target defines; test/conformance_vulkan_external.c defines them for the Vulkan driver -->
```c
mrhiResult mrhiConformanceDriver(mrhiExternalDriverDef* driverOut);
const mrhiChain* mrhiConformanceSurface(bool* fixedSizeOut);
```

with `-DMAUL_RHI_CONFORMANCE_DRIVER=<target>`. The build then adds `test_conformance_external`, which runs the suite on that driver. `mrhiConformanceSurface` returns the source of a window the driver's harness made, for the presentation case, or NULL for the build platform's own window system; `fixedSizeOut` says whether that window fixes its images' size.

- `test_conformance_external --list` prints the cases, `--case <name>` runs one, and every run ends with each case's outcome.
- Build the library with `MAUL_RHI_VALIDATION=ON` for the run. The validation layer then checks every answer the driver gives the core, and the `validation.clean` case requires that it counted no fault ([troubleshooting](troubleshooting.md#the-validation-layer)).
- The lists of earlier SPI versions stay as they were, so a driver built against an older interface knows exactly what it was admitted on.

`test/external/` in the repository builds a minimal driver against an install alone, which shows the shape of such a project.
