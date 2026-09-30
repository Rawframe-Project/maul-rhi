# Maul RHI records

The design records that apply to Maul RHI only. The family records
are listed in [README.md](README.md).

| Number | Title | Status |
|---|---|---|
| [mrhi-0001](mrhi-0001-library-profile.md) | Library profile | Accepted |
| [mrhi-0002](mrhi-0002-contract-schema.md) | The contract schema generates the public headers | Accepted |
| [mrhi-0003](mrhi-0003-drivers-and-adapters.md) | Drivers behind an SPI, adapters by request, and a test driver | Accepted |
| [mrhi-0004](mrhi-0004-devices.md) | Devices are made at once and ready later | Accepted |
| [mrhi-0005](mrhi-0005-defs-and-extension-chains.md) | Defs keep the family cookie and carry an extension chain | Accepted |
| [mrhi-0006](mrhi-0006-features-and-limits.md) | Features and limits, each mapped onto the four APIs | Accepted |
| [mrhi-0007](mrhi-0007-surfaces.md) | Surfaces from one chained native source, configured on a device | Accepted |
| [mrhi-0008](mrhi-0008-frame-graph.md) | The frame graph: declare, compile, record, submit | Accepted |
| [mrhi-0009](mrhi-0009-shader-containers.md) | Shader containers: both codes, one reflection, checked as hostile input | Accepted |
| [mrhi-0010](mrhi-0010-pipelines.md) | Pipelines: made asynchronously from a shared reflection | Accepted |
| [mrhi-0011](mrhi-0011-encoders.md) | Encoders: passes recorded into a frame's arena | Accepted |
| [mrhi-0012](mrhi-0012-queries.md) | Queries: sets on the device, written once a frame, resolved into buffers | Accepted |
| [mrhi-0013](mrhi-0013-submission.md) | Submission: a frame reaches its driver as one read-only view | Accepted |
| [mrhi-0014](mrhi-0014-device-loss.md) | Device loss: terminal, answered, reported | Accepted |
| [mrhi-0015](mrhi-0015-bindless-heaps.md) | Bindless heaps: one heap per pass, stable entries, sealed reads | Accepted |
| [mrhi-0016](mrhi-0016-web-without-emscripten.md) | The web without Emscripten | Accepted |
