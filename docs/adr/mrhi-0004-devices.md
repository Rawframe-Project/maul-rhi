# mrhi-0004. Devices are made at once and ready later

Status: Accepted

## Context

WebGPU gives a device only through a promise, while native APIs make
one at once. The family keeps roots as pointers the caller holds from
creation (family record 0016) and answers every request with exactly
one record in its owner's queue (family record 0018).

## Decision

- **Creation:** `mrhiCreateDevice` returns the device and a request id
  at once. The device opens through its driver, and the instance's
  queue answers the request with `mrhi_instanceDeviceReady` and the
  outcome: the device is then ready or failed (`mrhiGetDeviceState`).
- **Grants:** the def asks for features and limits, the floor by
  default. More than the adapter grants is `mrhi_errorUnsupported`;
  limits below the floor are invalid input.
- **Destruction order:** a device destroyed while opening answers its
  request with `mrhi_errorStale`. An instance with devices refuses to
  be destroyed, counting the call as misuse.
- **Misuse:** each device counts its own refused invalid calls
  (`mrhiGetDeviceMisuse`).

## Consequences

One opening shape serves the browser and native drivers. No request
goes unanswered, and no order of destruction can free what is still
in use.
