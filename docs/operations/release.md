# Releasing

Maul RHI follows Semantic Versioning with tags `vX.Y.Z`. Before 1.0.0 any release may change the API, the ABI and every data format.

## How a release happens

1. Pull requests merge to `main` with conventional commit titles (`feat`, `fix`, `perf` and the rest; see [AGENTS.md](../../AGENTS.md#pull-requests)).
2. release-please (`.github/workflows/release.yml`, configured by `release-please-config.json` and `.release-please-manifest.json`) keeps a draft release pull request open, titled `chore: release X.Y.Z`. It moves the version macros between the release-please markers in `include/maul-rhi/base.h` and writes a changelog entry from the commit titles.
3. Before merging it, rewrite its `CHANGELOG.md` entry in user language: what changed for someone using the library, grouped as Added, Changed, Fixed, Removed and Security. Leave out refactors, tests, CI and documentation. Name changes to the contract, the container format or the SPI and who they reach: programs, shader cooks, or drivers built outside the tree.
4. Merge it. The release workflow tags the version, publishes the GitHub release with the same notes, and attaches the source archive, a `SHA256SUMS` file, an SPDX SBOM and a build provenance attestation.

## Version bumps that go with a release

- **The ABI version** follows the library version by itself; nothing is bumped by hand.
- **The contract version** (`contract_version` in `contract/mrhi.json`, regenerated into `MRHI_CONTRACT_VERSION`) rises with any change to the public API's types or functions.
- **The SPI version** (`MRHI_SPI_VERSION` in `src/driver.h`) rises with any change to the installed SPI headers, and the mustpass list `conformance/mustpass/spi-<N>.txt` is renamed to it.
- **The container version** (1, in `src/container.c` and `tools/mrhi_container.py`) rises with any change to a container's bytes.

Each of these is part of the change that needs it, not of the release pull request.

## After a release

Libraries that fetch Maul RHI by tag move to the new tag in their own pull requests. Maul RHI's own seam check fetches Maul Window by tag (`test/seam/CMakeLists.txt`), and that tag moves by hand with each Maul Window release.
