# libapta_audio for the ESP Component Registry

`libapta_audio` is the standalone ESP-IDF distribution of the portable APTA
1.0 reference implementation. It contains the public C API, portable waveform,
tempo, beatgrid and container sources, the ESP-IDF adapter, configuration
options and a cooperative-scheduler example in one component directory.

The component requires ESP-IDF 5.5 or newer. Retained project CI covers
ESP-IDF 5.5.4 on ESP32 and ESP-IDF 6.0.2 on ESP32 and ESP32-S3. The optional
ESP-DSP dependency remains private and the portable scalar path is available by
default.

## Use a generated standalone package

From the `libapta-audio` source tree:

```bash
python3 ports/espidf/package_component.py \
  --source-root . \
  --output-dir build/component/libapta_audio \
  --archive build/libapta_audio-component.zip \
  --source-revision "$(git rev-parse HEAD)"
```

The archive has a single `libapta_audio-<version>/` root. To vendor it, extract
the component into an ESP-IDF project's `components/libapta_audio` directory.
The component then exposes:

```c
#include <apta/apta.h>
#include <apta/apta_espidf.h>
```

The complete adapter, memory-capability, Kconfig and cooperative-processing
documentation is in `PORTING.md` inside the generated package.

## Validate a package

The repository self-test builds the package twice and requires byte-identical
archives, a complete file inventory and safe archive paths:

```bash
python3 ports/espidf/test_package_component.py
```

With a current IDF Component Manager installation, lint the generated manifest:

```bash
compote manifest lint build/component/libapta_audio/idf_component.yml
```

The `ESP-IDF component distribution` GitHub workflow performs these checks and
builds the packaged cooperative example rather than the monorepo port path.

## Registry consumption

After a version has been published under an approved namespace, add it to an
ESP-IDF project with the registry namespace and component name:

```bash
idf.py add-dependency "<namespace>/libapta_audio^1.0.2"
```

or declare it in the consuming component's `idf_component.yml`:

```yaml
dependencies:
  <namespace>/libapta_audio: "^1.0.2"
```

Replace the example version with the published version. Registry publication is
not inferred from the repository package version; verify the component page or
Component Manager resolution before claiming that a version is available.

## Publication boundary

Registry versions are immutable distribution records and must correspond to an
exact repository tag. The manual `Publish ESP-IDF component` workflow therefore
requires a `vX.Y.Z` source tag whose value matches both `VERSION` and
`ports/espidf/idf_component.yml`. It generates the standalone package, lints
it, builds the example and only then invokes `compote component upload`.

The workflow needs the repository secret `IDF_COMPONENT_API_TOKEN` and an
approved registry namespace. Its dry-run mode should be used against the
staging registry before production publication.

P13 does not republish or modify `v1.0.1`; the first registry publication from
this pipeline must use a later release tag that already contains the P13
packaging implementation.

## Package provenance

`PACKAGE-MANIFEST.json` records the source revision and SHA-256 digest of every
file in the generated component. The standalone ZIP is deterministic for the
same source tree, revision and `SOURCE_DATE_EPOCH`. This package inventory is
additional evidence; the ESP Component Registry also computes its own package
checksums when processing an uploaded component.
