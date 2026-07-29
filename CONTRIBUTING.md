# Contributing to libapta-audio

Thank you for helping improve APTA. Contributions are welcome from individual
developers, product teams, researchers, documentation authors and platform
maintainers.

This project is still a 0.1 working draft. The specification, public API, ABI
and `.apta` format may change before APTA 1.0, but compatibility impact must be
made explicit in every proposal.

## Before you start

- Search existing issues and pull requests before opening a duplicate.
- Open an issue before substantial, breaking or normative work so the scope and
  compatibility impact can be agreed first.
- Small fixes, tests and documentation corrections may go directly to a pull
  request.
- Do not report suspected vulnerabilities in a public issue. Follow
  [`SECURITY.md`](SECURITY.md).

## Development setup

The native reference build requires:

- CMake 3.16 or newer;
- a C11 and C++11 compiler;
- GCC or Clang for sanitizer and fuzzing builds;
- a POSIX environment for the reference file adapter and command-line tools.

Configure, build and test the default native profile:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For portable-core work, disable POSIX adapters and tools:

```bash
cmake -S . -B build-core \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DAPTA_WARNINGS_AS_ERRORS=ON
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

For memory-safety or parser changes, also run a Clang sanitizer/fuzz build:

```bash
CC=clang CXX=clang++ cmake -S . -B build-sanitized \
  -DAPTA_ENABLE_SANITIZERS=ON \
  -DAPTA_BUILD_FUZZING=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitized --parallel
ctest --test-dir build-sanitized --output-on-failure
```

Platform-specific changes must also pass the relevant platform instructions.
ESP-IDF integration and its verified version range are documented in
[`ports/espidf/README.md`](ports/espidf/README.md).

## Making a change

1. Create a focused branch from the current `main`.
2. Keep each commit reviewable and give it an imperative, descriptive subject.
3. Add or update tests for behavior changes and parser edge cases.
4. Update the specification or informative documentation when a public
   contract, file-format rule, supported platform or command changes.
5. Run the smallest relevant test set during development and the complete
   applicable suite before submitting.
6. Open a pull request describing the problem, solution, compatibility impact
   and validation performed.

Generated build directories, local tool output and proprietary audio must not
be committed. Test fixtures must be reproducible and carry a redistribution
license or be generated entirely from repository-owned source.

## Code and API expectations

- Keep the portable core independent of filesystems, codecs, threads and
  operating-system APIs.
- Use checked arithmetic and explicit bounds for all untrusted container,
  metadata and PCM-derived sizes.
- Preserve immutable result ownership and the documented threading contract.
- Initialize public structures through the provided initializer functions and
  maintain `struct_size` and `api_version` compatibility.
- Use fixed-width types at API and interchange boundaries.
- Preserve deterministic serialization and strict malformed-input behavior.
- Add `SPDX-License-Identifier: Apache-2.0` to new source, header, build and
  test files using the comment syntax appropriate to the file.
- Avoid unrelated formatting or cleanup in a functional patch.

See [`docs/api/`](docs/api/), [`specification/`](specification/) and
[`docs/scheduler/APTA-SCHEDULER-POLICY-0.1.md`](docs/scheduler/APTA-SCHEDULER-POLICY-0.1.md)
for the detailed contracts.

## Specification and format changes

Normative behavior belongs under [`specification/`](specification/).
Informative implementation notes belong under [`docs/`](docs/).

A normative, public-API or `.apta` format change should include:

- the problem and intended use case;
- backward- and forward-compatibility analysis;
- resource and security impact;
- updated normative text;
- reference implementation support when applicable;
- positive, negative and interoperability/conformance tests;
- migration guidance for affected users.

Unknown optional data must remain skippable and unknown required data must fail
clearly. New format sections and versions must not silently reinterpret
existing bytes.

## Pull request checklist

- The change is focused and its motivation is clear.
- Public behavior and compatibility impact are documented.
- Tests cover success, failure and relevant boundary cases.
- All applicable local tests pass.
- No secrets, credentials, private audio or generated build artifacts are
  included.
- New files carry the correct license information.
- Documentation links and examples remain valid.

Maintainers may request changes or split an oversized pull request before
review. A pull request is merged only after required checks pass and a
maintainer approves it.

## Licensing contributions

The project is licensed under the
[Apache License 2.0](LICENSE). Under section 5 of that license, contributions
intentionally submitted for inclusion are provided under Apache-2.0 unless
explicitly stated otherwise. Only submit work that you have the right to
license. The project does not currently require a separate Contributor License
Agreement.

Project decision-making and maintainer responsibilities are documented in
[`GOVERNANCE.md`](GOVERNANCE.md).
