# HS-01 synthetic host HMM result

Date: 2026-10-26. Verdict: **HS01_SYNTHETIC_HOST_PASS_ONLY**.
Clean implementation commit: `7cd081f15f7e5a77af59f5bbd61c0158c65ac171`.
[Protocol](PRODUCT-HOST-HMM-PROTOCOL.md), [implementation](PRODUCT-HOST-HMM-IMPLEMENTATION.md),
[aggregate machine receipt](PRODUCT-HOST-HMM-RESULT.json).

All five fixed generated cases passed, once in registered order. Ordinary cases
score both frozen sparse graphs, select the expected meter, trace the winner and
require score-bit parity plus an independent every-edge/path-score check. Debug
and release tests cover the four frozen tiny HMM examples, independent monolithic
257/513-row parity, work quotas/block boundaries, invalid inputs and cancellation
in all phases. No neural weights, music, labels, saved logits, model framework or
physical device was used. This does not qualify musical accuracy or the P4 product.

## Measured host results

| Case | Rows | Meter | Worker wall s | Child CPU s | Child peak RSS KiB | Trace payload B |
|---|---:|---:|---:|---:|---:|---:|
| small3 | 257 | 3 | 0.208625 | 0.132359 | 18280 | 4578274 |
| small4 | 513 | 4 | 0.381227 | 0.297469 | 18144 | 6194466 |
| full3 | 30000 | 3 | 14.628027 | 14.541692 | 18328 | 12446880 |
| full4 | 30000 | 4 | 16.634031 | 16.507447 | 20172 | 16575840 |
| cancel | 30000 | cancel | 0.127030 | 0.022576 | 20416 | 16575840 |

The ordinary timings include both score-only passes and the winning trace; they
exclude audio decoding, frontend and neural inference because those stages were
not executed. Step-loop sums were 14.495573 s (full3) and 16.526283 s (full4); largest
observed 4096-unit step was 10.500622 ms. Allocation/constructor and supervisor times
are recorded separately in JSON. These are single observations on WSL/x86_64,
not hard worst-case or P4 latency bounds.

Cancellation request-to-return: **0.002772 ms**, below the registered host 50 ms gate.
Workspace drop/reclaim: **1.439928 ms**, measured separately. No partial result was exposed;
clearing the flag did not resume the cancelled decoder. The full4 owned trace
payload is 16575840 B (15.808 MiB), including the 60000 B path. Payload excludes graphs,
densities, allocator controls, stack and process/library overhead. Child rusage
RSS is a conservative process high-water (including pre-exec fork activity);
the independent sampled process-group envelopes are also retained. Neither is
whole-device/internal-RAM/PSRAM evidence.

## Integrity, enforcement and executed checks

The unchanged caps were CPU 120 s, AS 256 MiB, wall 180 s, per-process RSS 128 MiB and
aggregate output 1 MiB. All measured cases stayed within them. Fixed negative
wall/RSS/output/AS probes refused their violations. The RSS child reached 74228 KiB
against 16 MiB; refusal was verified on that allocating child. The 51 retained files
total 184301 B, including final aggregation, below 1048576 B.

The separate external audit rechecked 155 parent bindings, 129 loaded-runtime files per
worker, generated density hashes, child log hashes, exact limits, complete case
order and capacity/cancel arithmetic. Sources, caches, interpreter, linked/loaded
libraries, executable, protocol and public mathematical assets are bound. This
is supervision and input/API isolation, not an OS filesystem sandbox.

Executable SHA-256: `95b59ae4ae5537711e8d765ff97b9973cdce1d39a49e694795bad26f19fdffa2`.
Build: Cargo/Rust 1.95.0, release opt-in runtime binary; Python 3.12.3, WSL Ubuntu.
Full runtime identities and result hashes are in the machine receipt.

New tests: 13 debug + 13 release passes, zero ignored. Existing product-grid
controls: 12 passes. Default runtime context: 5 passes, 1 existing C-lifetime test
ignored because APTA_C_CONTEXT_LIFETIME_ORACLE is absent: **NOT_RUN**. fmt,
focused Clippy(-D warnings), release build and Python compile passed.

One prelaunch stopped before output creation/cases because the prior WSL/tmp
executable was absent. Its external incident receipt is retained. The same clean
source was rebuilt at `/home/daniel/libapta-hs01-build`, then the five measured
cases ran once; code, protocol and caps were unchanged. Initial development
RSS-supervisor repair is disclosed in the implementation receipt, before the run.

External evidence: `D:/AI/LIBAPTA/outputs/hs01-measured-20261026/`.
Manifest audit: `D:/AI/LIBAPTA/outputs/hs01-measured-20261026-audit.json`.

## Remaining product gates and handoff

HS-01 closes the registered host HMM prototype/parity/resource/cancel task only.
PF-04 (full product experiment) remains NOT_RUN. Preserve VERSION 1.0.1 and default
behavior. Beat-event correction/state-to-event/grid construction, frontend/LSTM,
compressed audio, progress/publication integration and a native P4 port are not
part of this result. No old experimental rejection is reclassified.

Next product prerequisite is evidence for BeatNet+ code/weights rights and
checkpoint training independence, alongside physical budgets when the confirmed
JC1060P470C_I_W_Y becomes available. Record exact silicon revision then; assumed
3.2 is unobserved. These remain LICENSE_UNRESOLVED, INDEPENDENCE_UNRESOLVED and
PF01 NEEDS_MEASUREMENT. Prepare a separately scoped registration before another
synthetic component/model task. H1 stays deferred; no model/corpus execution,
large port, provider activation, release, or P4 timing/memory PASS follows from HS-01.
