#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Opt-in exact neural evidence capture, no labels, scoring or new decoder."""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time

import apta_neural_dense as codec
import apta_1_1_neural_inference as legacy

ref = legacy.reference
PROTOCOL_PATH = ref.ROOT / "docs/rust/NEURAL-DENSE-CAPTURE-PROTOCOL.json"
PROTOCOL_SHA256 = "28490ab1983cac6abd011923c5f4e78d3e7bf5096fdb119a102c386e1de62685"
LEGACY_FREEZE_SHA256 = "0e0ab33d990e40253d61a6a39758fc30c33510238e02ad58d1467b7fa3d5e3ef"


def protocol():
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("dense-capture protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def event_record(beats, downbeats, frames):
    beats, boundary_beats = legacy.Models.source_domain(beats, frames / 48000)
    downbeats, boundary_downbeats = legacy.Models.source_domain(downbeats, frames / 48000)
    bf, df = [ref.rounded(t * 48000) for t in beats], [ref.rounded(t * 48000) for t in downbeats]
    ref.valid_events(bf, frames)
    ref.valid_events(df, frames)
    if not set(df) <= set(bf):
        raise ValueError("downbeat not on same learned event sequence")
    return {"beat_seconds": beats, "downbeat_seconds": downbeats, "beat_frames": bf, "downbeat_frames": df,
            "confidence": None, "input_sample_rate": 22050,
            "exclusive_endpoint_events": {"beat_seconds": boundary_beats, "downbeat_seconds": boundary_downbeats}}


class Tap:
    """Observe unchanged upstream calls; preserve their return objects."""
    def __init__(self, models):
        from beat_this.inference import split_piece
        self.models, self.split_piece = models, split_piece
        self.signal_original = models.beat_this.signal2spect
        self.frames_original = models.beat_this.spect2frames
        models.beat_this.signal2spect = self.signal
        models.beat_this.spect2frames = self.frames
        transform = models.beat_this.spect.spect_class
        spect = transform.spectrogram
        if (spect.n_fft != 1024 or spect.hop_length != 441 or not spect.center or spect.pad_mode != "reflect"
                or transform.n_mels != 128 or models.beat_this.frames2beats.fps != 50
                or models.beat_this.frames2beats.type != "minimal"):
            raise ValueError("upstream spectral/postprocessor geometry changed")
        self.clear()

    def clear(self):
        self.signal_calls, self.frame_calls = 0, 0
        self.mono_length = self.mono_sha256 = self.payload = self.assignment = None

    def signal(self, mono, rate):
        if rate != 22050 or mono.ndim != 1 or str(mono.dtype) != "float32" or not self.models.np.isfinite(mono).all():
            raise ValueError("legacy resampled input differs")
        self.signal_calls += 1
        self.mono_length = len(mono)
        self.mono_sha256 = hashlib.sha256(mono.astype("<f4", copy=False).tobytes()).hexdigest()
        return self.signal_original(mono, rate)

    def frames(self, spect):
        self.frame_calls += 1
        if self.mono_length is None or spect.ndim != 2 or spect.shape[1] != 128 or spect.shape[0] != self.mono_length // 441 + 1:
            raise ValueError("dense spectrogram geometry changed")
        chunks, starts = self.split_piece(spect, 1500, border_size=6, avoid_short_end=True)
        self.starts, self.lengths = [int(i) for i in starts], [len(chunk) for chunk in chunks]
        self.assignment = codec.coverage(spect.shape[0], self.starts, self.lengths)
        beat, downbeat = self.frames_original(spect)
        torch, np = self.models.torch, self.models.np
        if (beat.ndim != 1 or downbeat.ndim != 1 or len(beat) != spect.shape[0] or len(downbeat) != len(beat)
                or beat.dtype != torch.float32 or downbeat.dtype != torch.float32):
            raise ValueError("returned logit shapes/dtypes differ")
        self.payload = codec.encode(beat.detach().cpu().tolist(), downbeat.detach().cpu().tolist())
        decoded = codec.decode(self.payload)
        raw = np.stack([beat.detach().cpu().numpy(), downbeat.detach().cpu().numpy()], axis=1).astype("<f4").tobytes()
        if raw != self.payload[16:]:
            raise ValueError("serialized logit bits differ")
        if any(not torch.equal(original.cpu(), torch.tensor(saved, dtype=torch.float32)) for original, saved in zip((beat, downbeat), decoded)):
            raise ValueError("decoded logits changed")
        return beat, downbeat

    def replay(self, frames):
        if self.signal_calls != 1 or self.frame_calls != 1 or self.payload is None:
            raise ValueError("capture did not observe exactly one original forward")
        torch = self.models.torch
        beat, downbeat = codec.decode(self.payload)
        with torch.inference_mode():
            events = self.models.beat_this.frames2beats(torch.tensor(beat, dtype=torch.float32),
                                                       torch.tensor(downbeat, dtype=torch.float32))
        return event_record(*events, frames)


def synthetic(models):
    """Actual pinned-runtime operational evidence on generated signals only."""
    # Retain the existing neural/PF/reset operational checks unchanged.
    prior = models.preflight()
    tap = Tap(models)
    torch, np = models.torch, models.np
    evidence = []
    for name in ("silence", "pulse_noise"):
        mono = np.zeros(22050 * 2, dtype=np.float32)
        if name == "pulse_noise":
            mono = np.random.default_rng(20261023).normal(0, .005, len(mono)).astype(np.float32)
            mono[::11025] += .5
        tap.clear()
        models.reset()
        with torch.inference_mode():
            events = models.beat_this(mono, 22050)
        expected = event_record(*events, 96000)
        if tap.replay(96000) != expected:
            raise ValueError("synthetic replay differs from original events")
        geometry = codec.mapping(96000, len(mono), len(codec.decode(tap.payload)[0]),
                                 [{"first_frame": 0, "end_frame": 96000}])
        evidence.append({"name": name, "rows": geometry["rows"], "payload_bytes": len(tap.payload),
                         "bit_exact": True, "events_equal": True, "full_coverage": True})
    return tap, {"synthetic_only": True, "legacy_preflight": prior, "dense_fixtures": evidence}


def bind_inputs(assets, attempt):
    config, bindings = protocol(), {}
    paths = {"label_free_inputs_sha256": attempt / "inputs.private.json",
             "legacy_worker_report_sha256": attempt / "beat_this/report.json"}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    ref.bind(attempt / "freeze.private.json", LEGACY_FREEZE_SHA256, bindings)
    ref.bind(assets / "final0.ckpt", config["inputs"]["checkpoint_sha256"], bindings)
    if ref.revision(assets / "beat_this") != config["inputs"]["model_revision"]:
        raise ValueError("model source revision differs")
    frozen = json.loads((attempt / "freeze.private.json").read_text())
    if Path(frozen["assets"]).resolve() != assets.resolve():
        raise ValueError("legacy assets location changed")
    for name, digest in frozen["bindings"].items():
        path = Path(name)
        if path.resolve().is_relative_to(assets.resolve()):
            ref.bind(path, digest, bindings)
    value = json.loads(paths["label_free_inputs_sha256"].read_text())
    if (set(value) != {"format", "protocol_sha256", "prepared_manifest_sha256", "audio_root", "tracks"}
            or value["format"] != ref.FORMAT or value["protocol_sha256"] != ref.PROTOCOL_SHA256
            or len(value["tracks"]) != 100):
        raise ValueError("fixed label-free inputs required")
    report = json.loads(paths["legacy_worker_report_sha256"].read_text())
    ids = [row["track"] for row in value["tracks"]]
    if len(set(ids)) != 100 or ids != [row["track"] for row in report["outputs"]]:
        raise ValueError("fixed legacy cohort order differs")
    for row, old in zip(value["tracks"], report["outputs"]):
        if (set(row) != {"track", "file", "sha256", "requested_frames"} or Path(row["file"]).name != row["file"]
                or type(row["requested_frames"]) is not int or not 0 < row["requested_frames"] <= 1440000):
            raise ValueError("label-bearing or invalid input row")
        ref.bind(Path(value["audio_root"]) / row["file"], row["sha256"], bindings)
        ref.bind(attempt / "beat_this" / (row["track"] + ".json"), old["events_sha256"], bindings)
        coarse = attempt / "scoring" / (row["track"] + "-coarse.json")
        ref.bind(coarse, ref.history.sha256_file(coarse), bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    return value, bindings


def run(assets: Path, attempt: Path, output: Path, preflight_only=False):
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("output exists; refusing retry/overwrite")
    if not preflight_only and subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before real capture")
    revision = ref.revision(ref.ROOT)
    inputs, bindings = bind_inputs(assets, attempt)
    output.mkdir(parents=True)
    ref.history.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": PROTOCOL_SHA256, "bindings": bindings, "read_labels": False, "real_inference_started": False})
    models = legacy.Models(assets)
    tap, preflight = synthetic(models)
    ref.history.write_new(output / "preflight.json", preflight)
    if preflight_only:
        ref.recheck(bindings)
        return preflight
    signal.signal(signal.SIGALRM, legacy.timeout)
    started = time.monotonic()
    counts, channels, details = Counter(), {key: Counter() for key in ("beat", "downbeat")}, []
    slowest, output_bytes = 0., 0
    for row in inputs["tracks"]:
        track, frames = row["track"], row["requested_frames"]
        source = Path(inputs["audio_root"]) / row["file"]
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        if coarse["requested_frames"] != frames or coarse["sample_rate"] != 48000:
            raise ValueError("original source geometry mismatch")
        windows = [{key: w[key] for key in ("first_frame", "end_frame")} for w in coarse["windows"]]
        old = json.loads((attempt / "beat_this" / (track + ".json")).read_text())
        if old["audio_sha256"] != row["sha256"]:
            raise ValueError("legacy source audio differs")
        tap.clear()
        begin = time.monotonic()
        signal.alarm(config["limits"]["max_model_seconds_per_source"])
        try:
            events = models.infer("beat_this", source, frames)
            if events != old["events"] or tap.replay(frames) != old["events"]:
                raise ValueError("original/saved/decoded event parity failed")
        finally:
            signal.alarm(0)
        elapsed, rss = time.monotonic() - begin, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        if elapsed > config["limits"]["max_model_seconds_per_source"] or rss > config["limits"]["max_host_rss_kib"]:
            raise ValueError("registered per-source resources exceeded")
        slowest = max(slowest, elapsed)
        beat, downbeat = codec.decode(tap.payload)
        geometry = codec.mapping(frames, tap.mono_length, len(beat), windows)
        trace_path, record_path = output / (track + ".f32"), output / (track + ".json")
        with trace_path.open("xb") as stream:
            stream.write(tap.payload)
        saved_payload = trace_path.read_bytes()
        if saved_payload != tap.payload or codec.decode(saved_payload) != (beat, downbeat):
            raise ValueError("saved f32 payload identity mismatch")
        stats = codec.statistics(beat, downbeat)
        record = {"track": track, "audio_sha256": row["sha256"], "mono_f32_sha256": tap.mono_sha256,
            "geometry": geometry, "aggregation": dict(tap.assignment, starts=tap.starts, chunk_lengths=tap.lengths),
            "payload_sha256": ref.history.sha256_file(trace_path), "payload_bytes": len(saved_payload),
            "events": events, "exact_legacy_event_parity": True, "bit_exact_logit_roundtrip": True,
            "statistics": stats, "wall_seconds": elapsed, "rss_peak_kib": rss}
        ref.history.write_new(record_path, record)
        details.append({"track": track, "payload_sha256": record["payload_sha256"], "record_sha256": ref.history.sha256_file(record_path)})
        counts.update(sources=1, windows=len(windows), rows=len(beat), payload_bytes=len(saved_payload),
            source_owned_rows=sum(w["end_row"] - w["first_row"] for w in geometry["windows"]),
            exclusive_endpoint_rows=len(geometry["exclusive_endpoint_rows"]), beyond_source_rows=len(geometry["beyond_source_rows"]),
            legacy_beat_events=len(events["beat_frames"]), legacy_downbeat_events=len(events["downbeat_frames"]),
            exclusive_endpoint_beat_events=len(events["exclusive_endpoint_events"]["beat_seconds"]),
            exclusive_endpoint_downbeat_events=len(events["exclusive_endpoint_events"]["downbeat_seconds"]))
        for name, value in stats.items():
            channels[name].update({key: value[key] for key in ("values", "positive_logits", "nonpositive_logits")})
        output_bytes += len(saved_payload) + record_path.stat().st_size
        if time.monotonic() - started > config["limits"]["max_capture_seconds"] or output_bytes > config["limits"]["max_output_bytes"]:
            raise ValueError("registered run/output cap exceeded")
        print(json.dumps({"sources_complete": len(details), "rows": len(beat), "seconds": round(elapsed, 3), "legacy_parity": True}), flush=True)
    ref.recheck(bindings)
    for row in details:
        ref.bind(output / (row["track"] + ".f32"), row["payload_sha256"], {})
        ref.bind(output / (row["track"] + ".json"), row["record_sha256"], {})
    if ref.revision(ref.ROOT) != revision or subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("source changed during capture")
    elapsed, rss = time.monotonic() - started, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if (counts["sources"] != 100 or counts["windows"] != 600 or counts["rows"] != counts["source_owned_rows"] + counts["exclusive_endpoint_rows"] + counts["beyond_source_rows"]
            or elapsed > config["limits"]["max_capture_seconds"] or rss > config["limits"]["max_host_rss_kib"]
            or sum(p.stat().st_size for p in output.iterdir()) > config["limits"]["max_output_bytes"]):
        raise ValueError("complete cohort/resource check failed")
    result = {"format": config["format"], "source_revision": revision, "protocol_sha256": PROTOCOL_SHA256,
        "complete": True, "all_100_original_saved_and_decoded_events_identical": True,
        "all_f32_payloads_bit_exact": True, "all_rows_aggregation_assigned": True,
        "all_source_window_boundary_rows_retained": True,
        "read_labels": False, "musical_scoring": False, "new_decoder": False,
        "formal_acceptance": False, "production_promotion": False, "physical_p4": False,
        "counts": dict(counts), "logit_counts": {key: dict(value) for key, value in channels.items()},
        "resources": {"capture_wall_seconds": elapsed, "max_source_seconds": slowest, "rss_peak_kib": rss,
                      "source_output_bytes": output_bytes}, "details": details, "bindings": bindings,
        "synthetic_preflight_sha256": ref.history.sha256_file(output / "preflight.json")}
    estimated_report_bytes = len(json.dumps(result, indent=2, sort_keys=True).encode()) + 1024
    if sum(p.stat().st_size for p in output.iterdir()) + estimated_report_bytes > config["limits"]["max_output_bytes"]:
        raise ValueError("final report exceeds output cap")
    ref.history.write_new(output / "report.private.json", result)
    return {key: value for key, value in result.items() if key not in ("bindings", "details")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("assets", "attempt", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--preflight-only", action="store_true")
    args = parser.parse_args()
    try:
        result = run(args.assets, args.attempt, args.output, args.preflight_only)
    except Exception as exc:
        if args.output.is_dir() and not (args.output / "failure.json").exists():
            ref.history.write_new(args.output / "failure.json", {"complete": False, "error_type": type(exc).__name__, "message": str(exc)})
        raise
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
