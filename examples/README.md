# libapta examples

These examples are intentionally small and use only the stable public API.
They are built when `APTA_BUILD_EXAMPLES=ON`.

| Example | Purpose |
|---|---|
| [`pcm_push`](pcm_push/) | Feed host-owned interleaved PCM blocks into a session and read the final overview waveform. |
| [`pcm_pull`](pcm_pull/) | Expose PCM through callbacks and let the cooperative scheduler request bounded chunks. |
| [`installed_cmake_consumer`](installed_cmake_consumer/) | Consume an installed package through `find_package(APTA CONFIG)`. |
| [`pkgconfig_consumer`](pkgconfig_consumer/) | Compile against an installed Unix package through `pkg-config`. |
| [`espidf/cooperative_scheduler`](espidf/cooperative_scheduler/) | Build the retained ESP-IDF cooperative scheduling integration. |

Build the in-tree desktop examples:

```bash
cmake -S . -B build-examples \
  -DAPTA_BUILD_EXAMPLES=ON \
  -DAPTA_BUILD_TESTS=ON \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF
cmake --build build-examples --parallel
ctest --test-dir build-examples -L examples --output-on-failure
```

The example programs generate PCM in memory, so they do not require audio files,
codecs or platform adapters.
