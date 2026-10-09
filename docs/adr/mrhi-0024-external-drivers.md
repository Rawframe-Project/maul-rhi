# mrhi-0024. Drivers built outside the tree

Status: Accepted

## Context

Console drivers are closed implementations of the driver SPI that can
never enter this repository. mrhi-0003 decided that drivers are
compiled in, since the web loads no code at run time, and that a
driver from outside is handed to the instance through a critical
chained struct; neither the struct nor installed SPI headers existed.
bgfx and SDL 3 admit their console renderers at source level, by
replacing a stub file or defining a private bootstrap and rebuilding
the library against its internal header; neither checks a version.
The requirements ask for a version and size handshake whose mismatch
is a typed failure, and for mustpass lists per SPI version.

## Decision

- `mrhiExternalDriverDef` (`mrhi_structExternalDriver`) on an instance
  def carries a driver's instance vtable and its own pointer, made by
  the driver's code before the instance. The instance starts it
  instead of the build's driver, owns it once created (destroying it at
  its end), and leaves it to the program when creation fails. It is
  refused beside the test driver or the Vulkan driver's structs. The
  core reports its adapters as `mrhi_driverExternal`.
- The handshake: a vtable of another `MRHI_SPI_VERSION` is
  `mrhi_errorVersion`; one smaller than the core knows, or lacking a
  function, is `mrhi_errorInvalid`. A device's vtable is checked when
  the device is made, and a refused device is destroyed through its
  first function, which `destroy` stays in every version.
- The SPI headers (`driver.h`, `command.h`, `reflection.h`,
  `container.h`, `container_interface.h`) are installed as
  `maul-rhi/spi/`. Their types are the SPI; any change to them raises
  `MRHI_SPI_VERSION`, now 5 (4 was the first installed; 5 adds the
  shader features a container's code needs and the blend source of its
  color outputs). The core functions they declare are not part of it.
- The conformance suite's checks are named cases in the ten categories
  of the requirements (`api.objects`, `binding.heaps`, ...).
  `test_conformance --list` prints them, `--case <name>` runs one, and
  every run ends with each case's outcome. The list of the current SPI
  version, `conformance/mustpass/spi-5.txt`, must match the suite's
  listing (the `mustpass` test); lists of earlier versions stay as
  they were. A driver is admitted on a run that passes every case of
  its version's list. Device loss is injected with
  `mrhiSimulateDeviceLoss` (`loss.simulated`), and two passes record
  at once on two threads (`threads.recording`; interleaved on one in
  the web builds, which have no threads).
- `MAUL_RHI_CONFORMANCE_DRIVER` names a target that defines
  `mrhiResult mrhiConformanceDriver(mrhiExternalDriverDef* driverOut)`;
  the suite is then built again as `test_conformance_external` and run
  on that driver. Without one, Linux builds hand the suite their own
  Vulkan driver through this path, so it runs in CI. The target also
  defines `const mrhiChain* mrhiConformanceSurface(bool* fixedSizeOut)`:
  the source of a window its harness made, presented to by
  `swapchain.present`, or NULL for the build platform's window system.
- Chained struct types with bit 30 set (`MRHI_STRUCT_DRIVER_DEFINED`)
  belong to outside drivers: an instance of such a driver takes one as
  a surface's source and hands it to the driver unexamined; every other
  instance and def refuses it as an unknown critical struct.

## Consequences

A console driver is a library of its own, built against an install of
Maul RHI and linked into the program, with no file of this repository
replaced, and presents through source structs of its own. A test builds such a driver against an install alone. The
shader container carries code for the four open APIs only; a driver
of another API needs its own code in containers, decided when one
exists.
