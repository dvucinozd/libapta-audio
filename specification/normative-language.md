# Normative language

**Status:** APTA 1.0 Release Candidate Draft

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHALL**, **SHALL NOT**, **SHOULD**, **SHOULD NOT**, **RECOMMENDED**, **MAY** and **OPTIONAL** are normative requirement terms when written in uppercase.

## Requirement strength

- **MUST** and **REQUIRED** describe an unconditional conformance requirement.
- **MUST NOT** describes an unconditional prohibition.
- **SHOULD** describes a strong recommendation. A conforming implementation may deviate only when the consequences are understood and documented.
- **SHOULD NOT** describes behaviour that is strongly discouraged.
- **MAY** and **OPTIONAL** describe permitted behaviour.

Lowercase uses of these words are descriptive and are not automatically normative.

## Defined specification terms

- **implementation-defined** means the implementation chooses the behaviour and MUST document that choice.
- **unspecified** means a conforming implementation may choose among allowed behaviours and is not required to document the choice.
- **invalid** means input violates a requirement and MUST be rejected with an applicable error.
- **unsupported** means input is valid but the implementation does not provide the requested optional capability.

## Numeric notation

Unless explicitly stated otherwise:

- integer ranges are inclusive;
- source-frame ranges are half-open: `[first_frame, end_frame)`;
- sizes are measured in octets;
- binary integer fields in `.apta` files use explicitly specified endianness;
- integer overflow MUST be detected before performing an unsafe allocation, offset calculation or conversion.
