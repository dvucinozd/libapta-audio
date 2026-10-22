# Sparse PCM attack lattice experiment

Current status: [recorded development coverage rejects promotion](S6-RECORDED-DEVELOPMENT.md).
The 36/36 synthetic result below remains valid only for its sparse generated
fixtures; the frozen candidate emits no grid on 360 recorded-prefix windows.

Pre-registered 2026-10-22 before candidate evaluation. Baseline is
`1876eb34a9169299ece0d908927850e6f6e79670` plus the retained PCM-meter local
inventory SHA-256 `36d432fb3b824b1e79b51a05bfd3017a309f7772e8ff5fb747ba9dc1fd266abc`.
The exact dirty baseline is archived outside the repository. Candidate feature:
`experimental-s6-attack-grid`, default off, plus explicit per-history opt-in.

## Hypothesis and frozen rules

Read-only traces of the unchanged estimator confirm two distinct failures in
the prior three generated cases. At 8 kHz the selected lag 2 is correct, but the
16-period refinement picks extended lag 24 and clamps to -0.5, moving 117188 to
156251 millibpm. At 48 kHz the weighted lag-16 score 0.483442664 narrowly exceeds
lag-8 score 0.481373310, selecting half tempo (87891 instead of 175781).
Do not alter that estimator, its priors or thresholds in this experiment.

Use the newly captured 256-frame PCM positive-amplitude-change evidence instead.
Record the first positive change's sample offset inside each bin, retaining the
existing 16-byte host bin layout. An attack event starts at a nonzero bin after
a zero bin; consecutive active bins belong to one event. A cluster already
active before a window is skipped; unavailable predecessor evidence rejects.
The anchor is the first observed event, never an assumed downbeat.

Require 8–128 observed events. Derive the mean interval from first/last event and
require **every** adjacent interval within 1% of that mean, using integer
arithmetic. Convert the interval to nearest millibpm in 40–300 BPM. Require all
events within 0.10 beat of the exact resulting Q32 lattice. These tolerances
come from the existing period/phase contract, not observed candidate scores.
No missed-beat interpolation, amplitude-weighted tempo prior, half/double-family
rescue or fallback to the old grid is allowed. Confidence is capped at 49; all
results remain provisional. Silence, dense continuous attacks, too few/many
events, lost bins and irregular spacing remain explicit unsupported outcomes.
The same no-missing-beat rule covers both window edges: a complete missing beat
before the first or after the last event (outside the 0.10-beat tolerance) rejects
the whole window. A regular burst cannot authorize a silent remainder.

Expected cost: no extra attack-ring bytes or heap allocation, <=1024 fine-bin
checks plus <=128 interval/phase checks per window, <=1024 bytes of temporary
event positions plus ordinary call temporaries, and caller-sized selection
records retaining both baseline and candidate verdicts. Work is linear and
bounded per existing history step; hardware deadlines are not measured here.

## Evidence, comparison and veto

Keep the original three failed PCM fixtures and their default-history verdicts.
Freeze a separate 36-case generated challenge before candidate scoring:
sample rates 8000/44100/48000, tempos 90/123/168 BPM, meters 3/4 and 4/4,
rectangular/decaying tone bursts, non-bin-aligned origins and deterministic
bounded jitter. Check exact grid/meter binding as well as independent period,
cyclic beat/downbeat phase and meter truth. Archive the parameter manifest/hash.
This is synthetic development transfer, not a fresh musical acceptance corpus.

Also exercise silence, missing/extra/irregular attacks, boundary-spanning
clusters, actual ring eviction, capacity, one-step scheduling, unknown duration,
retained copies and zero allocation. The candidate may abstain when its sparse
evidence assumptions fail; coverage losses are reported separately.

Veto/stop: changed default/history-without-opt-in results, hidden allocation,
invented support or source phase, confidence >=50, unbounded work or mutation on
failure. Report candidate fixes, breaks and abstentions against the frozen
baseline. Any newly wrong emitted lattice/meter or loss of a baseline-correct
challenge case vetoes promotion; do not tune thresholds after results. Formal
corpora/holdouts remain unopened, P4 and product-adoption gates remain open.

## Implemented diagnostic candidate

Enable the Cargo feature and attach `MeterHistory` to `WindowHistory`, then call
`WindowHistory::enable_attack_grid` before attaching to Session. Caller selection
storage must cover the entire history capacity; failed attachment leaves it
untouched. `Session::global_history_selection` exposes source-window records
containing the unchanged coarse-estimator verdict and the new verdict.
Compilation alone does not select this path. History without explicit opt-in
retains the original estimator and its known failures.

On acceptance the source window uses the observed anchor and selected tempo.
On failure it publishes a gap/unsupported verdict; there is no coarse fallback.
The existing exact meter scorer then consumes that selected grid, and the
independent provisional publication copies it without promoting state. Callers
must use a new grid revision for this different interpretation; tests use 20
instead of baseline 19. Production Session GGRD/REVN and wire limits are unchanged.

All three original PCM reproductions now have correct period, cyclic beat phase,
meter and downbeat under the frozen diagnostic tolerances, in known/unknown
duration profiles. The frozen 36-case parameter manifest has SHA-256
`6774353677c003b3ff050b72d2de51ab2e35eb865cad458878400ec5d2765856`.
Complete grid/meter/downbeat success improves from 2/36 to 36/36: 34 fixes,
zero breaks and zero abstentions on this deliberately sparse generated set.
These are synthetic development results, not independently labeled recordings
or a formal acceptance run. No thresholds changed after challenge inspection.
These 36 cases are now open development/regression fixtures, never fresh
acceptance evidence. Endpoint gap checks enforce the original no-invented-support
veto; they were made explicit during static review after the initial challenge
pass. Initial and final logs are retained, with the same manifest and tolerances.

Boundary tests separately reject missing/extra attacks, regular fragments with
silent uncovered tails/heads, dense continuous activity, invalid/replaced bin
identities, out-of-range tempo and event limits. A cluster already active in
the preceding bin is skipped. With expired-clock input starvation, actual ring
eviction yields `Unavailable` and later preserved windows agree with prompt
capture. Silence and short EOF do not acquire invented timing. Retained output,
capacity and lifecycle protections from the preceding publication remain active.

Host layout remains 16 bytes per attack bin (32768-byte ring). A selection record
is 64 bytes and event scratch is 1024 bytes; the new feature adds its optional
trace slice to history control. The implementation uses two scans of at most
1024 bins (identity/count preflight, then event collection) and two validation
passes of at most 128 events, rather than the initially forecast single bin
scan. Work remains bounded/linear; the unchanged coarse estimator still runs
to retain baseline verdicts. No host
runtime measurement establishes a P4 deadline or memory-placement guarantee.

Reproduction:

```sh
cargo test -p libapta --features experimental-s6-attack-grid --locked \
  --test history_lattice -- --nocapture
```

Baseline snapshot, read-only trace source/log, challenge manifest, tested source
and verification reports are external under
`D:/AI/LIBAPTA/outputs/rust-s6-selection-20261022/` and
`/home/daniel/.local/share/libapta-audio/rust-rewrite/selection-20261022/`.
The next promotion gate is independent recorded development evidence, including
non-sparse/syncopated material and abstention/false-lattice rates. A sequence of
equally spaced acoustic attacks does not by itself establish musical beat level.
Keep defaults and product adoption unchanged until that evidence exists.

## Exact candidate verification

Rust 1.95.0 / WSL x86-64 passes 289 default, 315 history-only and 320 all-feature
workspace tests in each debug/release profile; 58 focused all-feature tests pass
AddressSanitizer and leak detection. All-target Clippy with warnings as errors,
rustfmt and default/all-feature no-default-library compilation pass. Explicit
attack-grid capture and conversion allocate nothing after caller storage setup.
Host history control is 584 bytes with this feature, including its optional
selection trace. All changed code hashes match the immutable tested snapshot.
The report verifies every challenge parameter against the frozen manifest and
checks that the original `estimate_window` body remains unchanged.

Full C/oracle/CLI interchange, the old 129-window history case and the actual
consumer probe were not rerun for this opt-in selection change; their earlier
evidence remains historical. Recorded-music coverage, false-lattice and
confidence qualification, minimum toolchain/other platforms, physical P4 and
trusted native-result/product adoption remain open. No release/version change,
commit or push is part of this local development checkpoint.
