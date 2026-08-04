# APTA 1.0 normative manifest

**Status:** Final APTA 1.0 manifest — editorial revision 1  
**Release:** APTA 1.0 / libapta 1.0.1  
**Hash domain:** Git blob SHA-1 of UTF-8 file content

## 1. Authority

The files below are the complete normative APTA 1.0 set. A path not listed
here is informative unless one of these documents normatively incorporates it.

The manifest does not list its own blob hash because that would be
self-referential. The governance-approved `v1.0.0` release tag identifies the original
published manifest instance. The `v1.0.1` tag identifies editorial revision 1
and its corrected file hashes.

## 2. Normative files

| Path | Title | Git blob SHA-1 |
|---|---|---|
| `APTA-SPEC.md` | Adaptive Progressive Track Analysis Specification | `3ffbc65316c5352aa2ad39531057e5bdda9a6b31` |
| `normative-language.md` | Normative language | `2dfd6d49ca00c794b157af5ad43975d76acad80c` |
| `terminology.md` | APTA terminology | `5e06d8a9133c37a4cc38d06f174a12fa23e833c8` |
| `time-model.md` | Source time model | `666851c52b9d067a6b700e711bcd7b28cfd1fbd9` |
| `pcm-input.md` | PCM input contract | `b37e169dd5f51e90c1221fd13ee3f6780d92c8eb` |
| `progressive-scheduling.md` | Progressive and adaptive scheduling model | `66459d5aa00a1e51df5a27ce6a8e993549a2fa15` |
| `waveform.md` | Waveform model | `e5bdf3063ab18441ba43802be9991299fa609365` |
| `tempo.md` | Tempo model | `bf572a69e9c1242ecd9ded4b8e64a8ee5cc803e5` |
| `beatgrid.md` | Beatgrid model | `843b28a22c4cf995b2374cee8a25dae2ba478545` |
| `lifecycle.md` | Analysis lifecycle | `5f3e92124016b876920e949954f762619b78e443` |
| `confidence.md` | Confidence model | `538711a97dfd18fda6ee5e6203207ec6f7d1fbbf` |
| `result-model.md` | Result and snapshot model | `2ad5d6809e640602f715d5bd6fb73741fc7a4b45` |
| `container-v1-registry.md` | Container version 1 section registry | `8107c9c7e9638d34ffdf2d075f490c4ace2e7d03` |
| `file-format.md` | `.apta` container format | `3aa2f41608310073e3db082117f6b3a4706c0a1e` |
| `global-grid-container.md` | Global-grid and revision container sections | `33253d15128e0ffa81a3515592b750429e6a9277` |
| `wovr-state-flags.md` | WOVR version-1 lifecycle-state flags | `a3377a2bd2bbc85c88d7981d8b141157d5ee8a37` |
| `profiles.md` | Conformance profiles and resource classes | `c9358d7504cfb280be6edb39ebbe9546eb8e37d7` |
| `extensions.md` | Extension model | `f62f69552e9aae7d27bceb1d869d376750a3efd1` |
| `conformance.md` | Conformance model | `0c2f7f8d676f2569db01064f5e8c812e2fba9ff4` |

## 3. Change control

Any change to a listed file changes its blob hash and requires a manifest
update. A normative change also requires compatibility impact, implementation
and conformance review under project governance. Editorial corrections may
change hashes without changing the specification version only when they do not
alter requirements.

The `v1.0.0` tag remains the immutable first published APTA 1.0 authority.
The `v1.0.1` tag publishes editorial revision 1 together with
[`APTA-1.0-ERRATA-1.md`](APTA-1.0-ERRATA-1.md). The corrections do not alter
normative semantics and never rewrite the tagged `v1.0.0` release.
