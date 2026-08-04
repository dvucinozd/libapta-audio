# APTA 1.0 normative manifest

**Status:** Final APTA 1.0 manifest  
**Release:** APTA 1.0  
**Hash domain:** Git blob SHA-1 of UTF-8 file content

## 1. Authority

The files below are the complete normative APTA 1.0 set. A path not listed
here is informative unless one of these documents normatively incorporates it.

The manifest does not list its own blob hash because that would be
self-referential. The governance-approved `v1.0.0` release tag identifies the
exact published manifest instance.

## 2. Normative files

| Path | Title | Git blob SHA-1 |
|---|---|---|
| `APTA-SPEC.md` | Adaptive Progressive Track Analysis Specification | `e0eb760d5bcdef55d5c7bf8bd1aff2c981f5f686` |
| `normative-language.md` | Normative language | `c7528d4f9cdb66fb9241d4f72e98c682b05b26a2` |
| `terminology.md` | APTA terminology | `28b8fa0f41dd75d0165b7cdddf8b8dc79f84eb5e` |
| `time-model.md` | Source time model | `90b56401697878b2cbc20d56e6c8fe8da91f1ed7` |
| `pcm-input.md` | PCM input contract | `d01dc7c37b14b98657b508331d23302b93959bff` |
| `progressive-scheduling.md` | Progressive and adaptive scheduling model | `cd16ed2363d44c33937c9e0c9e5f50dfd0d36c8e` |
| `waveform.md` | Waveform model | `e66f5a667729610483a17d3cfae28ffd3341b63b` |
| `tempo.md` | Tempo model | `39b75ad9dc51b8089499b529cbea5a648003242a` |
| `beatgrid.md` | Beatgrid model | `35b64bcdea6a8c2b42164027b4160a43b7a5376b` |
| `lifecycle.md` | Analysis lifecycle | `b62560f8e600db8aae484cd3c6258511b38babc7` |
| `confidence.md` | Confidence model | `6b06fde78f91813192fa48c9ed0243e8b49452a3` |
| `result-model.md` | Result and snapshot model | `cfda09e9369c53b86619bcf1a2b501ed2a329578` |
| `container-v1-registry.md` | Container version 1 section registry | `0bc7aa7199f341498d7a4c45c7442f7bc08d021e` |
| `file-format.md` | `.apta` container format | `4ddbe68f9b89be7c5a10eaca90cd1d7e0dedf8cc` |
| `global-grid-container.md` | Global-grid and revision container sections | `9e2362e06282a597a8eac0502988e2739947773c` |
| `wovr-state-flags.md` | WOVR version-1 lifecycle-state flags | `7ba8c516ad73c3532b737d289b1a9e53cbb4ef5c` |
| `profiles.md` | Conformance profiles and resource classes | `faf9aa1bfc005354a0e4baed15b154499ab96dcc` |
| `extensions.md` | Extension model | `f4db847e8b30ed1a85ac464e1d64c2bfe8f22273` |
| `conformance.md` | Conformance model | `af6b308e8bed8fa6bd15364efbaad0ab717821f7` |

## 3. Change control

Any change to a listed file changes its blob hash and requires a manifest
update. A normative change also requires compatibility impact, implementation
and conformance review under project governance. Editorial corrections may
change hashes without changing the specification version only when they do not
alter requirements.

The `v1.0.0` tag freezes this manifest as the published APTA 1.0 authority.
Later compatible corrections are published through the documented 1.x change
process and never silently rewrite the tagged release.
