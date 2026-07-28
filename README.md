# libapta-audio

`libapta-audio` is the home of the Adaptive Progressive Track Analysis (APTA) standard and its portable ISO C11 reference implementation.

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
