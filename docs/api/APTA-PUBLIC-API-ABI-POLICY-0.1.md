# APTA public API and ABI policy 0.1

**Status:** Design policy draft  
**Applies to:** future public headers under `include/apta/`

## 1. Compatibility domains

APTA distinguishes:

1. source-level C API portability;
2. binary ABI compatibility on a defined target architecture and calling convention;
3. `.apta` file-format interoperability across platforms.

A single binary library is not expected to be ABI-compatible across different CPU architectures, pointer widths or operating-system calling conventions. File-format interoperability is independent of native ABI layout.

## 2. Language boundary

Public headers target ISO C11 and MUST compile as C++ through `extern "C"` guards.

```c
#ifdef __cplusplus
extern "C" {
#endif

/* public declarations */

#ifdef __cplusplus
}
#endif
```

The implementation MAY use newer language features internally when the selected build configuration permits them.

## 3. Symbol visibility and calling convention

Every exported function MUST use project macros:

```c
#ifndef APTA_API
#define APTA_API
#endif

#ifndef APTA_CALL
#define APTA_CALL
#endif
```

Platform ports define these macros for import/export visibility and calling convention. Public declarations MUST NOT rely on compiler defaults when a platform requires explicit decoration.

## 4. Opaque objects

Public object types are opaque:

```c
typedef struct apta_context apta_context_t;
typedef struct apta_session apta_session_t;
typedef struct apta_result apta_result_t;
```

Applications MUST NOT allocate, copy, inspect or free opaque objects directly.

## 5. Fixed-width public values

Public structures use `<stdint.h>` fixed-width integer types for externally meaningful values.

Public structures MUST NOT store a C enum, C `bool`, compiler bit-field or native `long` where layout stability matters.

Named constants may be declared with macros or anonymous enums, while storage uses a fixed-width typedef:

```c
typedef uint32_t apta_result_state_t;
#define APTA_RESULT_STATE_EMPTY 0u
```

Pointers and `size_t` necessarily follow the target ABI and therefore do not imply cross-architecture binary compatibility.

## 6. Extensible structures

Every extensible public structure begins with:

```c
uint32_t struct_size;
uint32_t api_version;
```

Rules:

- callers set `struct_size` to `sizeof(the_structure)`;
- callers set `api_version` using the public API version macro;
- initialiser functions SHOULD be provided for every non-trivial configuration structure;
- the library validates the minimum size needed for fields it reads;
- new fields are appended only;
- the library ignores unknown trailing caller fields;
- callers initialise reserved fields to zero;
- the library MUST NOT read beyond `struct_size`;
- incompatible reinterpretation of an existing field requires an ABI major version change.

Example:

```c
APTA_API void APTA_CALL
apta_session_config_init(apta_session_config_t *config);
```

## 7. Alignment and packing

Public native structures use the platform's normal ABI alignment. They MUST NOT be declared with packed layout.

Native C structure layout MUST NOT be used directly as `.apta` serialized layout. Serialization uses explicit field encoding and offsets.

## 8. Function result convention

Fallible functions return `apta_status_t`, a signed fixed-width integer type:

```c
typedef int32_t apta_status_t;
```

- zero represents success without additional immediate work requirements;
- positive values represent non-error status conditions;
- negative values represent errors.

Output parameters MUST remain in a documented safe state after every return. Functions MUST document whether partial output can accompany a positive status or error.

## 9. Ownership naming

Public function names SHOULD reveal ownership operations:

- `create` / `destroy` for exclusive objects;
- `acquire` / `release` for shared immutable snapshots;
- `borrow` only for explicitly scoped non-owning access;
- `copy` when caller-owned storage receives copied data.

A getter returning an internal pointer MUST document the pointer's lifetime and thread-safety.

## 10. Callback rules

Every callback structure begins with `struct_size`, `api_version` and `user_data`.

Unless a callback contract explicitly says otherwise:

- callbacks execute synchronously on the thread calling the APTA function;
- callback pointers and `user_data` remain valid until the owning context or session is destroyed;
- callbacks MUST NOT destroy the object currently invoking them;
- callback re-entry into the same session is prohibited;
- callback re-entry into unrelated sessions is implementation-defined until the threading specification is complete;
- the library MUST NOT retain a borrowed callback buffer beyond the corresponding release operation.

## 11. Threading baseline

The baseline core guarantees:

- distinct sessions may be used concurrently only when the context advertises that capability;
- one session is processed by at most one processing thread at a time;
- cancellation request is thread-safe;
- immutable result acquisition and reading are thread-safe;
- configuration mutation is not thread-safe unless explicitly stated.

A dedicated threading specification will define memory ordering and destruction rules before ABI stabilisation.

## 12. Version macros

Public headers will expose independently named versions:

```c
#define APTA_SPEC_VERSION_MAJOR 0
#define APTA_SPEC_VERSION_MINOR 1

#define APTA_API_VERSION_MAJOR  0
#define APTA_API_VERSION_MINOR  1
#define APTA_API_VERSION_PATCH  0
```

The `.apta` container version is not inferred from these macros.

## 13. Deprecation

A public symbol MAY be deprecated in a minor release but MUST remain available for the documented compatibility window.

Removal or incompatible signature change requires a major API/ABI version change.

## 14. ABI stabilisation gate

The project MUST NOT claim stable ABI status until:

- all public structures follow this policy;
- 32-bit and 64-bit layout tests exist;
- C and C++ compile tests exist;
- symbol visibility is tested on supported shared-library platforms;
- ownership and threading contracts are complete;
- at least one minor-version forward-compatibility test exists.
