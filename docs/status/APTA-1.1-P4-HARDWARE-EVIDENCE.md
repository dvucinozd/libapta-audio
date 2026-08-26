# APTA 1.1 ESP32-P4 hardware evidence contract

**Status:** pre-registered physical-device evidence contract; no qualifying hardware run has been recorded yet.

This contract closes only the physical ESP32-P4 evidence blocker. Existing CI already proves that the cooperative example builds for `esp32p4` under ESP-IDF 6.0.2 and that the 30-minute bounded layout fits the configured design ceilings. Physical evidence must additionally prove that the exact qualified firmware executes on a real board without allocation failures, deadline misses or input drops while the intended USB/audio workload coexists.

## Required run

The tracked evidence JSON must use schema `apta-1.1-esp32-p4-hardware-evidence-1` and record:

- exact 40-hex source revision and SHA-256 of the flashed firmware artifact;
- board model/revision, operator and test location;
- `idf_target=esp32p4`, ESP-IDF `6.0.2`, PSRAM enabled and 48 kHz input;
- at least 1,800 seconds of continuous execution;
- all release-target DJ features enabled: overview/detail/three-band waveform,
  BPM, local/global beatgrid, dynamic tempo, confidence, grid locking,
  meter/downbeat, musical key and calibrated BPM quality;
- 32,768 overview frames per column (the frozen 30-minute P4 profile);
- actual workspace/result-pool sizes;
- actual overview-column and resident-beat-record counts;
- free/minimum-free internal heap and PSRAM measurements;
- process-call p99 and maximum latency observations;
- allocation-failure, deadline-miss and input-drop counters;
- an explicit USB/audio coexistence result and completed-run flag.

## Fail-closed gates

`tools/apta_1_1_p4_hardware_evidence.py` rejects evidence unless all of the following hold:

- source revision is a full SHA-1 and, when supplied to the validator, matches the expected qualified revision;
- firmware SHA-256 is valid lowercase hex;
- ESP-IDF/target/sample-rate/PSRAM values match the frozen target;
- duration is at least 30 minutes;
- workspace is at least 941,216 bytes and result pool at least 537,104 bytes;
- overview columns are at most 4,096 and resident beat records at most 9,216;
- measured minimum free internal heap and PSRAM remain positive and cannot exceed their pre-test values;
- process p99 cannot exceed the observed maximum;
- all required DJ features are present;
- allocation failures, deadline misses and input drops are exactly zero;
- USB/audio coexistence and the complete test run are explicitly marked passed.

The contract deliberately records process latency instead of inventing a hardware latency threshold before measurements exist. A later release candidate may define an additional performance target only through a separately reviewed, pre-registered criterion; the existing evidence must not be post-hoc reinterpreted.

## Boundary

A synthetic JSON document that satisfies the shape is not physical evidence. The release blocker may be marked closed only after the values are captured from the exact flashed build and the resulting JSON is reviewed and committed as `evidence/1.1/esp32-p4-hardware.json`.
