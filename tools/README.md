# Reference desktop tools

The desktop tools are informative integrations built on the same stable
`apta::core` implementation. They are not separate DSP implementations and are
not stable exported package components.

Build them with the native adapters:

```bash
cmake -S . -B build-tools \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=ON \
  -DAPTA_BUILD_TOOLS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-tools --parallel
```

The current tool targets include:

- `apta-analyze` — analyse supported input through the reference decoder path;
- `apta-inspect` — inspect result/container metadata and sections;
- `apta-validate` — validate canonical container structure and semantics;
- `apta-version` — print package, API, specification and container versions;
- workspace and cost probes used for retained implementation evidence.

Run each executable with `--help` for the exact options compiled by the current
source revision. Platform adapters own file and decoder integration; the
portable core itself does not open files or decode codecs.
