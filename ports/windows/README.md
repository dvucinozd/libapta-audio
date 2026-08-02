# Windows platform adapter

The Stage S8 Windows integration binds the portable APTA core and desktop
tools to Win32 without using ESP-IDF or POSIX compatibility APIs.

It provides:

- UTF-8 path conversion to native UTF-16;
- checked 64-bit read-only file access through `CreateFileW`;
- flushed sibling-temporary writes followed by
  `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`;
- the reference WAV pull decoder and `apta-analyze`, `apta-inspect` and
  `apta-validate` executables built with MSVC;
- native adapter, decoder, pull-analysis and CLI interchange tests.

Configure from a Visual Studio developer shell:

```powershell
cmake -S . -B build-windows -DAPTA_BUILD_TESTS=ON -DAPTA_WARNINGS_AS_ERRORS=ON
cmake --build build-windows --config Release --parallel 2
ctest --test-dir build-windows -C Release --output-on-failure
```

The shared public adapter header is `apta/desktop/apta_file.h`. Its path
contract is UTF-8 on both Windows and POSIX.
