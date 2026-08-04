# Push PCM example

`main.c` demonstrates the smallest complete host-driven analysis flow:

1. create a context and push-mode session;
2. submit bounded interleaved `int16_t` PCM blocks;
3. signal the final source length;
4. drive cooperative processing to completion;
5. acquire and inspect the immutable overview-waveform result.

From the repository root:

```bash
cmake -S . -B build-examples \
  -DAPTA_BUILD_EXAMPLES=ON \
  -DAPTA_BUILD_TESTS=OFF \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF
cmake --build build-examples --target apta_pcm_push_example
./build-examples/examples/apta_pcm_push_example
```

On multi-config generators, run the executable from the selected configuration
directory, such as `build-examples/examples/Release/`.
