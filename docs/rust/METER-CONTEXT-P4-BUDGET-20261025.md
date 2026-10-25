# Current Ruzina analysis budget audit

Date: 2026-10-25. [Source bindings and decision](METER-CONTEXT-P4-BUDGET-20261025.json).
Read-only consumer inspection uses fetched `origin/main`
`d000572c15b370702345f36d4dcc11a410b2f1de`. Its local checkout remains at
`0f2fb1425ef76f2541a8c76841cfa52178dca589`; existing untracked work is preserved.
No consumer branch, files, dependency pin, provider or firmware is changed.

The current requirements, architecture, Guition matrix and adapter README still
define stopped-deck analysis, cancellation during playback, immutable accepted
analysis, generation safety and continuous final full-source grid/meter. Optional
APTA is inactive on Guition. Requirement M1R-P1-001 defers adoption until upstream
embedded/release gates pass.

The Guition source matrix documents16MiB flash and32MiB PSRAM. These capacities
are not free analysis allocations or physical unit qualification. The64KiB CPU-
cached80MHz PSRAM probe registers no allocator. The adapter documents113772bytes
for two diagnostic local-tempo output banks and4096bytes of Embassy payloads,
117868bytes combined. Stack, futures, channels, retained copies and the full
neural pipeline are additional; these figures establish no product budget.

| Required decision/evidence | Current state | Closure needed before large product port |
| --- | --- | --- |
| Internal RAM / analysis PSRAM reserve | UNKNOWN | Exact-unit memory qualification and allocation after audio/USB/UI reserves |
| Stack, futures, channels and retained banks | UNKNOWN total | Complete ownership/lifetime inventory and target high-water measurements |
| Model weights, native inference backend and activations | UNKNOWN | Accepted backend plus storage and peak overlap measurements |
| Decoder scratch/output placement | UNKNOWN product limit | Bound storage class, lifetimes and full-track/gap limits |
| First/useful/final latency and processing time per audio minute | UNKNOWN | Numeric product deadlines and exact-device measurements |
| Cancellation deadline | Policy known, numeric limit UNKNOWN | Bound cancellation work and measure target response |
| Audio/USB interference | No regression required; numeric analysis allowance UNKNOWN | Exact-image integrated physical qualification |

Decision: bounded generated host work may continue. A large product-directed
Rust/P4 port remains blocked by missing numeric product budgets and whole-pipeline
evidence. The product owner must select those limits against actual qualified
hardware reserves; engineering must measure the complete implementation against
them. No arbitrary limit is inferred from board capacity or Python RSS.

This refresh preserves the historical DD-00 constraints/evidence. It does not
qualify the current consumer build, full-length continuity, physical P4 or a new
model. Those are separate gates, alongside independent musical transfer and
fresh final acceptance. VERSION remains1.0.1.
