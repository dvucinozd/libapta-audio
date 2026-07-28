# libapta-audio

`libapta-audio` is the home of the Adaptive Progressive Track Analysis (APTA) standard and its portable ISO C11 reference implementation.

APTA (Adaptive Progressive Track Analysis) aims to provide a highly optimized, progressive, and adaptive framework for processing and analyzing audio tracks. Built from the ground up for performance and portability, it allows real-time insights, multi-level waveform processing, and precise track evaluations suitable for diverse audio domains. Whether running on constrained embedded devices or high-end desktop environments, `libapta-audio` leverages advanced algorithms to deliver reliable, progressive audio analysis with minimal overhead.

> Project status: architecture and specification development.

## Repository layout

- [`specification/`](specification/) — normative APTA specification documents.
- [`docs/`](docs/) — architecture, API, format, porting, review and roadmap documentation.
- [`include/apta/`](include/apta/) — public C API headers.
- [`src/`](src/) — platform-neutral library implementation.
- [`backends/`](backends/) — replaceable DSP backends.
- [`ports/`](ports/) — platform integration layers.
- [`tools/`](tools/) — command-line tools.
- [`tests/`](tests/) — unit, integration, conformance, fuzz and golden-vector tests.
- [`examples/`](examples/) — usage and platform examples.
- [`packaging/`](packaging/) — build-system and package-manager integration.

## Current architecture draft

The original project architecture document is preserved at:

[`docs/architecture/APTA-ARCHITECTURE-DRAFT.md`](docs/architecture/APTA-ARCHITECTURE-DRAFT.md)

It remains an architecture draft and input to the future normative specification.

## Building the project

The project uses CMake as its build system. To build the library and its tests, run:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### CMake Options

You can customize the build by setting various CMake options:

- `APTA_BUILD_TESTS` — Build `libapta` tests (default: `ON`).
- `APTA_ENABLE_SANITIZERS` — Enable AddressSanitizer and UndefinedBehaviorSanitizer for robust debugging (requires GCC or Clang) (default: `OFF`).
- `APTA_BUILD_FUZZING` — Build libFuzzer targets (default: `OFF`).

Example with sanitizers enabled:

```bash
cmake -DAPTA_ENABLE_SANITIZERS=ON -DAPTA_BUILD_TESTS=ON ..
cmake --build .
```

## Testing

If you have built the project with `APTA_BUILD_TESTS=ON`, you can easily run the test suite via CTest:

```bash
cd build
ctest --output-on-failure
```

## License and Copyright

Please see the [`LICENSE`](LICENSE) file for details (MIT / Apache-2.0).

Copyright (c) Daniel Vučinović.