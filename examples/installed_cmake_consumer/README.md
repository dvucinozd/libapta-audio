# Installed CMake consumer

This directory is a standalone project that consumes the installed
`APTAConfig.cmake` package and stable `apta::core` target.

```bash
cmake -S ../.. -B /tmp/apta-build \
  -DAPTA_BUILD_TESTS=OFF \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/apta-prefix
cmake --build /tmp/apta-build --parallel
cmake --install /tmp/apta-build

cmake -S . -B build -DCMAKE_PREFIX_PATH=/tmp/apta-prefix
cmake --build build --parallel
./build/apta_installed_consumer
```

For multi-config generators, add `--config Release` to build and install
commands and run the executable from `build/Release/`.
