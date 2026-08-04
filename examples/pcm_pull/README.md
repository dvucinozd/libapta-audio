# Pull PCM example

`main.c` demonstrates callback-based PCM ownership. The host exposes an in-memory
source through `apta_pcm_source_t`; each `apta_session_process()` call is bounded
by `maximum_input_frames` and `maximum_steps`.

From the repository root:

```bash
cmake -S . -B build-examples \
  -DAPTA_BUILD_EXAMPLES=ON \
  -DAPTA_BUILD_TESTS=OFF \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF
cmake --build build-examples --target apta_pcm_pull_example
./build-examples/examples/apta_pcm_pull_example
```

Real hosts may return `APTA_STATUS_WOULD_BLOCK` while asynchronous decoding or
I/O catches up. They retain ownership of the block until the paired
`release_frames` callback.
