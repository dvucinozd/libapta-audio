# P13 — Ecosystem distribution

**Status:** source controls complete; first registry publication pending a later stable tag and owner credentials.

## Objective

Turn the existing ESP-IDF integration into a reproducible, standalone component
distribution without changing the stable APTA 1.0 specification, public API,
shared ABI, container bytes or DSP behaviour.

## Delivered

- deterministic rewriting of the monorepo ESP-IDF CMake root into a
  self-contained component root during packaging;
- deterministic `libapta_audio-<version>` component generation;
- a complete package inventory with per-file SHA-256 digests;
- a registry-facing README, API document, changelog, license and packaged
  cooperative-scheduler example;
- expanded registry metadata in `idf_component.yml`;
- a package self-test that checks reproducibility, required files and archive
  path safety;
- an automatic distribution workflow that lints the generated manifests and
  builds the packaged example with the retained ESP-IDF 6.0.2 image;
- a manual registry-publication workflow that accepts only an exact stable tag
  matching `VERSION` and the component manifest;
- staging/production selection, dry-run support and retained publication
  evidence.

## Distribution boundary

The source-tree port remains usable through `EXTRA_COMPONENT_DIRS`. The P13
package is different: it carries all portable sources and public headers inside
the component root, so it does not depend on paths outside an extracted archive
or managed-component directory.

The generated package name is `libapta_audio`. A registry consumer will use the
approved namespace together with that component name.

## Release boundary

P13 does not change package `1.0.1`, public API `1.0.0`, `SOVERSION 1`,
container version 1, canonical `.apta` bytes or production analysis behaviour.
The existing `v1.0.1` tag and release remain immutable.

The registry workflow intentionally cannot publish P13 from `v1.0.1`, because
that tag predates this packaging implementation. The first production registry
version must come from a later exact `vX.Y.Z` tag containing P13.

## Validation authority

- `ports/espidf/test_package_component.py` is the deterministic package gate;
- `ESP-IDF component distribution` lints the generated manifests and builds the
  packaged cooperative example;
- the existing ESP-IDF matrix remains the authority for the supported
  IDF/target combinations;
- `Publish ESP-IDF component` requires an exact tag, matching versions, an
  approved namespace and `IDF_COMPONENT_API_TOKEN`.

P13 becomes operationally complete when a later stable tag is dry-run against
the staging registry and then published to production by the repository owner.
