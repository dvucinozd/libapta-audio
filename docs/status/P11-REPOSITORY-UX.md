# P11 — Repository UX and contributor onboarding

**Status:** complete.

## Objective

Make the stable library understandable, buildable and contributable by a new
user without weakening the frozen APTA 1.0 compatibility boundary.

## Delivered

- expanded README with project motivation, a five-minute quick start, platform
  matrix, version-domain explanation, help paths and example navigation;
- buildable push-mode and pull-mode desktop examples using only the public API;
- standalone installed-CMake and pkg-config consumer projects;
- `APTA_BUILD_EXAMPLES` CMake option and CI compilation/execution on Linux and
  Windows;
- support, citation, conduct, ownership, pull-request and structured issue
  templates;
- reference desktop-tools overview;
- stable-release workflow gating that avoids rebuilding an already-published
  package for documentation and repository-only changes.

## Compatibility boundary

P11 changes repository presentation, examples and contribution workflows only.
It does not change production DSP, public API/ABI, canonical `.apta` bytes,
container version, specification requirements or package version.

## Validation

- examples compile with warnings as errors against the in-tree core;
- push and pull examples execute as CTest examples;
- installed CMake consumer builds and runs against a staged installation;
- pkg-config consumer builds and runs against the installed `libapta.pc`;
- issue-form, citation and workflow YAML files are parsed during local review;
- the normal CI and ESP-IDF workflows remain the merge authority.
