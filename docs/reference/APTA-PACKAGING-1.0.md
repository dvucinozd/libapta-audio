# APTA 1.0 package contract

**Status:** release-candidate package contract  
**Package version:** 1.0.0-rc.1  
**Public CMake target:** `apta::core`

## Scope

The installable 1.0 package contains the portable APTA core, its public headers,
CMake package metadata, pkg-config metadata and the Apache-2.0 license. Native
file adapters, the WAV reference decoder and command-line tools remain source
components in this release-candidate package and are not exported as stable
package components.

The package never exports internal sanitizer, profiling, fuzzing or C11-atomics
helper targets. Consumers link only the public core target and platform system
libraries required by a static build.

## Configure, build and install

Static package:

```sh
cmake -S . -B build-static \
  -DAPTA_BUILD_TESTS=OFF \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-static
cmake --install build-static --prefix /chosen/prefix
```

Shared package:

```sh
cmake -S . -B build-shared \
  -DBUILD_SHARED_LIBS=ON \
  -DAPTA_BUILD_TESTS=OFF \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-shared
cmake --install build-shared --prefix /chosen/prefix
```

For multi-configuration generators, pass `--config Release` to both build and
install commands.

## CMake consumer

```cmake
find_package(APTA 1.0 CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE apta::core)
```

`APTAConfig.cmake` also exposes:

- `APTA_VERSION_FULL`, including the release-candidate suffix;
- `APTA_SHARED_PACKAGE`, indicating whether the installation is shared;
- `APTA_RUNTIME_DIR`, the installed runtime-library directory.

The package version file uses same-major compatibility. The public API's own
major/minor compatibility and `struct_size` rules remain authoritative for C
callers.

## pkg-config consumer

```sh
cc consumer.c $(pkg-config --cflags --libs libapta) -o consumer
```

Use `pkg-config --static` for a static link. On Unix, `libapta.pc` reports the
private math-library dependency for static consumers.

## Installed layout

Common files:

```text
include/apta/*.h
lib/cmake/APTA/APTAConfig.cmake
lib/cmake/APTA/APTAConfigVersion.cmake
lib/pkgconfig/libapta.pc
share/licenses/libapta/LICENSE
```

Unix static builds install `lib/libapta.a`. Unix shared builds install the
unversioned linker name, the ABI-major soname and the full-version library.
The 1.x ABI policy is `SOVERSION 1`; the current full filename version is
`1.0.0` while the package metadata retains `1.0.0-rc.1`.

Windows static builds install `lib/apta.lib`. Windows shared builds install
`bin/apta.dll` and the import library `lib/apta.lib`. No debug postfix is part
of the 1.0 contract; debug and release packages must use separate install
prefixes when both are retained.

## Source and embedded integration

Source vendoring remains supported through `add_subdirectory()` and the
build-tree `apta::core` alias. ESP-IDF applications continue to consume the
component under `ports/espidf`; the native CMake package does not replace the
ESP-IDF component manifest or component build.

## Verification

The normal CI matrix configures a clean external consumer against both the
build-tree package and a staged installation. The install test executes the
public context-create/destroy smoke API. Linux additionally compiles and runs
the same source through `pkg-config`; shared Windows verification executes the
consumer with the installed DLL.

## Release archives and checksums

After configuring and building a release tree, generate the native binary and
source archives with:

```sh
cmake --build build-static --target package
cmake --build build-static --target package_source
```

The binary archive name contains the full package version, operating system and
processor. The source archive uses `libapta-<version>-source`. CPack emits a
`.sha256` sidecar for every archive. Release verification sets
`SOURCE_DATE_EPOCH` and compares independently generated binary and source
archives byte-for-byte; it also verifies that VERSION, CHANGELOG.md, LICENSE,
CMake package metadata and pkg-config metadata are present where applicable.
