# HS-05 artificial recurrent synthetic host result

Date: 2026-10-27. Verdict: **HS05_SYNTHETIC_HOST_PASS_ONLY**.
[Protocol](PRODUCT-HOST-NEURAL-PROTOCOL.md),
[implementation](PRODUCT-HOST-NEURAL-IMPLEMENTATION.md),
[machine result/artifact manifest](PRODUCT-HOST-NEURAL-RESULT.json).

All five cases ran once in registered order from clean pushed implementation
`a7fad1e887172e1bb955bc8199d32f0a8e8a642d`. A new exclusive external binary
copy was bound before measurement with six components,protocol,case book,
all28 new assets,135 prior assets,40 authority files,actual Rust/Cargo tools,
feature,linked libraries and Python/runtime sources/caches/libraries. There
was no in-attempt repair,retry,backend/tolerance/parameter/cap change or subset.

| Case | Observations | Committed rows | Worker wall seconds | Combined worker + child CPU seconds |
|---|---:|---:|---:|---:|
| fixtures |46 (34 probes,12 sequences)|90|0.718|0.148|
| long-zero |1|30000|2.958|2.846|
| long-carry |1|30000|2.995|2.909|
| long-reset |1|30000|3.129|2.912|
| cancel |1|1351 provisional|0.385|0.195|

Every full case executed2280000 affine MACs and5160000 step units,including
the zero profile. The reset case additionally charged one legal reset before
row15000,total5160001 units. The checker compares every probability and all8
state values,stable argmax,input prefix,counts,reset schedule and terminal
snapshot. All exact primitive bit gates and scalar numeric gates pass. All
three complete candidate trace digests also match the frozen normative trace
digests exactly;that identity is a result,not a substitute for the numeric gate
or a new requirement to match across different libm implementations.

Measured Session.step totals for full cases were0.043889–0.057619s;largest
observed bounded step0.076065ms. CLI pipe writes consumed2.512–2.625s,with
checker comparisons/waits separately recorded. Worker wall includes source
generation,child/transport/checking and final validation. This is a small
three-input/two-hidden/two-layer toy with76 affine MACs/row,not the approximately
23-billion-MAC full-size trained graph. Do not extrapolate these timings to
the full graph or claim product first5s/final60s/cancel50ms readiness on P4.

The asynchronous request10ms after bounded processing began was serviced in
AcceptRow after1351 completed rows (accepted==committed). Request-to-return
was2.246270ms,including host driver/pipe scheduling before the next bounded
call. Status was Cancelled,no final certificate,no resume after flag clearing,
and the terminal state exactly matches the last emitted prefix state. Scope end
took0.000119ms;this is not device heap reclamation/publication quiescence.
Other phase/partial-state cancellation paths are covered by preflight tests,
not claimed as this asynchronous measurement's observed phase.

Host Session value352B,ProbeSession512B,Driver1464B;array payload256B and profile
bank1808B remain separate. Input buffer768B,output12B,snapshot32B and51B packing
record are bounded. No full-sequence input/state/probability tensor is retained.
Runtime components use fixed safe storage,without allocator calls or retained
caller references. These are type-layout/source facts,not physical stack peaks.

All CPU30s combined plus inherited hard limits,wall45s,RSS64MiB,AS128MiB and1MiB
retained-output gates pass. The four deliberate operational violations are
refused. Worker/waited-child HWM is at most19972KiB (19.51MiB);the waited-child
measurement includes fork/exec and must not be described as isolated Rust RAM.
The getrusage CPU snapshot precedes final runtime hashing/receipt writes. A
separate full-lifetime audit bounds combined CPU by supervised wall times two
single-thread case processes (times three for the cancel requester). Every bound
is below30s,including those trailing writes;the monitor parent is excluded as
specified. The machine result binds this supplemental audit and its values.
Compiler actually used:Rust1.95.0;Clippy honors declared MSRV1.81. A separate
build using rustc1.81 was NOT_RUN. All probe/sequence/control/lint evidence and
the two disclosed pre-freeze repairs remain in the implementation receipt.
One absent C-context-oracle control remains NOT_RUN,not PASS.

Audit verifies51 measured artifacts/307401B,347 parent bindings and655 worker
runtime entries (131 per worker,all prebound). Every worker runtime and all
bindings were rechecked;source was clean/stable through completion. There are
50 validated observations and91441 emitted rows including the1351-row cancelled
prefix. Header/trailer bytes,incremental transport/trace digests,last records,
raw stderr and resource receipts are preserved. No failure artifacts were lost.

Raw evidence:
`D:/AI/LIBAPTA/outputs/product-host-neural-run-a7fad1e-20261027/`.
Independent audit:
`D:/AI/LIBAPTA/outputs/product-host-neural-audit-a7fad1e-20261027.json`.

This closes only HS05's registered artificial host numerical experiment.
VERSION1.0.1,default/core/wire/consumer and HS01-04 sources remain unchanged.
No trained checkpoint,music/training/labels,device,provider or release ran.
Rights/training independence,full-size graph parity,PF01 physical budgets and
product PF04 remain open;Guition JC1060 unavailable,revision unobserved,H1 deferred.

## Next handoff

Audit remaining full-graph coverage and the open rights/resource gates before
registering another implementation. HS05 does not implement Conv1d/ReLU/pool,
278->150 affine shapes,four150-unit LSTM layers or trained-checkpoint loading.
Compare those gaps and their existing static cost/lifetime ledger with the
completed host modules. Record what can be verified with artificial inputs,
what needs rights/provenance closure,and what needs actual JC1060 memory/timing.
This is a static readiness/scope decision;no automatic full-size graph port,
checkpoint execution,new model or corpus is enabled by the HS05 pass.
