# pkg-config consumer

This standalone Unix example consumes the installed `libapta.pc` file.

```bash
cmake -S ../.. -B /tmp/apta-build \
  -DAPTA_BUILD_TESTS=OFF \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/apta-prefix
cmake --build /tmp/apta-build --parallel
cmake --install /tmp/apta-build

export PKG_CONFIG_PATH=/tmp/apta-prefix/lib/pkgconfig:$PKG_CONFIG_PATH
make
./apta-pkgconfig-consumer
```

Some systems install `libapta.pc` under `lib64/pkgconfig`; adjust
`PKG_CONFIG_PATH` when necessary.
