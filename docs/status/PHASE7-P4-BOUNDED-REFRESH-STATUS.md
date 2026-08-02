# Phase 7 — bounded P4 refresh status

**Status:** implementation and single-board measurement complete
**Measurement date:** 2026-08-02
**Source parent:** `34b1118` plus the Phase 7 working branch
**Board:** ESP32-P4 revision 1.3 on COM12
**Firmware:** ESP-IDF 5.5, scalar backend, release build

## 1. Problem closed by this phase

S4 tempo and S6 global-grid refreshes were gated by evidence growth, but a
refresh that did run was monolithic. On the measured P4, the previous full
profile averaged 5,852 us and reached 19,323 us in one
`apta_session_process()` call. That maximum was below the 21,333-us duration of
a 1024-frame block at 48 kHz, but left only 9.4% headroom and did not honor the
call's step budget while inside the correlation scan.

Phase 7 makes those refreshes cooperative without changing the public API or
the final semantic result.

## 2. Scheduler model

The existing `apta_work_budget_t.maximum_steps` and
`soft_time_budget_us` fields now cover the complete process call, including S4
and S6:

- S4 flux preparation is one step;
- one S4 correlation step scans four ordered lags;
- S4 refinement, family selection, phase calculation and commit are one final
  step;
- S6 flux preparation is one step;
- one S6 step analyzes one compile-time-bounded 128-bin window;
- S6 fallback, aggregation commit and revision publication are one final step.

An absolute process deadline is created once at the public process boundary
and shared by waveform, S4 and S6 work. S4 leaves a share of a finite
multi-step budget for downstream S6. During final drain, a completed final S4
generation is reused instead of being redundantly restarted, so S6 cannot be
starved even when `maximum_steps == 1`.

## 3. Atomicity and lifecycle

S4 keeps the frozen evidence range, next lag and ordered argmax candidates in
private session state. S6 keeps its pending segments and aggregation counts in
private S6 state. Cached and published results change only in the final commit
step.

Consequently:

- an acquired result never sees a partially scanned evidence generation;
- focus changes can update the applicability range while an expensive refresh
  remains private;
- grid locking cancels pending S4 work safely;
- cancellation between steps returns `APTA_ERROR_CANCELLED` without committing
  partial analysis;
- end-of-input remains in `DRAINING` until both refresh engines have completed;
- locked local grids and unlocked tempo/local/global grids reach `FINAL`.

No heap allocation was added to `apta_session_process()`.

## 4. Targeted regression evidence

`apta.scheduler.cooperative_refresh` exercises the deliberately hostile
`maximum_steps = 1` case. It verifies:

1. every process call reports at most one completed step;
2. S4 and S6 mutation serials stay unchanged while their private generations
   remain active;
3. final drain terminates and publishes final tempo, local grid and global
   grid;
4. a cancellation issued during an active S4 generation commits no partial
   result;
5. one-step and unbounded final refreshes produce identical semantic tempo and
   grid output;
6. an expired soft deadline reaches the analysis wrapper, reports
   `MORE_WORK`, and performs zero analysis steps.

The existing focus test accepts `MORE_WORK` while confirming immediate range
publication. The revision/locking test now also confirms that the locked local
grid reaches `FINAL`.

Host validation on the Phase 7 branch:

| Configuration | Result |
|---|---:|
| Windows UCRT GCC core-only release | 71/71 |
| Linux default desktop/tools release | 83/83 |
| ASan + UBSan core-only debug | 71/71 |
| Experimental multiband onset release | 72/72 |

All builds enabled compiler warnings as errors.

## 5. ESP32-P4 measurement

The cooperative example processed eight seconds of 48-kHz mono click-track
PCM in 1024-frame blocks with `maximum_steps = 8`. Its p99 is the upper edge of
a 100-us fixed histogram bucket, so the reported p99 is conservative by at
most 99 us.

| Feature set | Workspace | Previous avg | Phase 7 avg | p99 upper | Previous max | Phase 7 max |
|---|---:|---:|---:|---:|---:|---:|
| overview | 68,688 | 1,940 | 1,916 | 3,100 | 3,056 | 3,026 |
| overview + confidence | 68,688 | 1,946 | 1,918 | 3,100 | 3,078 | 3,031 |
| + BPM | 150,704 | 3,553 | 2,288 | 6,300 | 16,223 | 7,170 |
| + local grid | 150,704 | 3,556 | 2,288 | 6,300 | 16,224 | 7,170 |
| + global grid | 603,504 | 4,147 | 2,670 | 6,000 | 16,910 | 10,295 |
| + dynamic tempo | 603,504 | 4,153 | 2,674 | 6,000 | 16,902 | 10,321 |
| + detail + locking | 611,744 | 5,852 | 3,657 | 8,200 | 19,323 | 12,355 |

Times are microseconds per `apta_session_process()` call. The full profile's
average fell 37.5% and its maximum fell 36.1%. Its worst measured call now has
8,978 us, or 42.1%, of a 21,333-us audio block left as scheduling margin. The
conservative p99 upper bound has 61.6% margin.

The final result remained 125,000 millibpm at confidence 98. Every feature row
reported zero heap delta. The complete run reported:

```text
free_heap_before=34150887 free_heap_after=34150887 delta=0
minimum_free_heap=33551583
largest_free_block=33030144
stack_high_water_words=5776
```

The workspace increase is bounded private scheduler state: 64 bytes for
profiles through local grid and 976 bytes for S6/full profiles. The former is
the opaque session state; the latter also includes pending S6 segments and
aggregation counts. The old S6 refresh's capacity-sized temporary window array
was removed from the process stack.

## 6. Acceptance result and remaining boundary

The measured acceptance targets are met on this board/configuration:

- full average <= 5 ms: **3.657 ms**;
- full p99 <= 12 ms: **<= 8.2 ms**;
- full maximum <= 15–16 ms: **12.355 ms**;
- audio-block margin >= 25%: **42.1%**;
- process-time heap allocation: **none observed and bounded tests remain green**.

This is strong single-board evidence, not a hard-real-time certification. It
does not yet cover a 30-minute playback/watchdog soak, concurrent hardware
decoder and USB/filesystem traffic, other P4 clock/PSRAM configurations, or
independent on-device `.apta` interchange.
