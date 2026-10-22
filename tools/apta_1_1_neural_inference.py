#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CPU-only fixed upstream models; no labels or APTA baseline enter inference."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time

import apta_1_1_neural_reference as reference


class Models:
    def __init__(self, assets: Path):
        import numpy as np
        import torch
        self.np, self.torch, self.assets = np, torch, assets
        self.config = reference.protocol()
        torch.set_num_threads(self.config["resources"]["torch_threads"])
        self.bindings = {}
        for name, folder in (("beatnet", "BeatNet"), ("beat_this", "beat_this")):
            if reference.revision(assets / folder) != self.config["models"][name]["revision"]:
                raise ValueError("model source revision differs")
            # Bind all Python source; untracked build metadata is not model code.
            for path in (assets / folder).rglob("*.py"):
                if ".git" not in path.parts:
                    reference.bind(path, reference.history.sha256_file(path), self.bindings)
        reference.bind(assets / "final0.ckpt", reference.FINAL0_SHA256, self.bindings)
        weights = assets / "BeatNet" / self.config["models"]["beatnet"]["weights"]
        reference.bind(weights, self.config["models"]["beatnet"]["weights_sha256"], self.bindings)
        sys.path.insert(0, str(assets / "BeatNet/src"))
        from BeatNet.model import BDA
        from BeatNet.log_spect import LOG_SPECT
        import BeatNet.particle_filtering_cascade as pf
        self.pf = pf
        self.front = LOG_SPECT(sample_rate=22050, win_length=1411, hop_size=441, n_bands=[24], mode="online")
        self.beatnet = BDA(272, 150, 2, "cpu")
        self.beatnet.load_state_dict(torch.load(weights, map_location="cpu", weights_only=True), strict=True)
        self.beatnet.eval()
        from beat_this.inference import Audio2Beats
        self.beat_this = Audio2Beats(checkpoint_path=str(assets / "final0.ckpt"), device="cpu", float16=False, dbn=False)

    def reset(self):
        self.beatnet.hidden = self.torch.zeros(2, 1, 150)
        self.beatnet.cell = self.torch.zeros(2, 1, 150)
        seed = self.config["models"]["beatnet"]["rng_seed"]
        self.np.random.seed(seed)
        self.pf.rng = self.np.random.default_rng(seed)

    def decoder(self):
        return self.pf.particle_filter_cascade(beats_per_bar=[], fps=50, plot=[], mode="online")

    def preflight(self) -> dict:
        """Neural chunk state, reset and decoder parity; synthetic inputs only."""
        torch, np = self.torch, self.np
        generator = torch.Generator().manual_seed(12345)
        features = torch.randn(1, 73, 272, generator=generator)
        with torch.inference_mode():
            self.reset()
            whole = self.beatnet(features).clone()
            self.reset()
            chunks = torch.cat([self.beatnet(features[:, i:i + 7]).clone() for i in range(0, 73, 7)], dim=2)
            delta = float((whole - chunks).abs().max())
            if not torch.allclose(whole, chunks, atol=1e-5, rtol=1e-5):
                raise ValueError("BeatNet chunked recurrent state differs")
            self.reset()
            reset = self.beatnet(features).clone()
            if not torch.equal(whole, reset):
                raise ValueError("BeatNet reset is not repeatable")
        activations = np.full((300, 2), .1)
        activations[:, 0] = .9
        activations[::100, 1] = .95
        self.reset()
        batch = self.decoder().process(activations)
        self.reset()
        decoder = self.decoder()
        for i in range(len(activations)):
            streamed = decoder.process(activations[i:i + 1])
        if len(batch) < 2 or not np.array_equal(batch, streamed):
            raise ValueError("PF batch/chunk events differ with frozen seed")
        # Silence is synthetic operational evidence, not an accuracy test.
        with torch.inference_mode():
            b, d = self.beat_this(np.zeros(22050 * 2, dtype=np.float32), 22050)
        self.validate_seconds(b, 2)
        self.validate_seconds(d, 2)
        reference.recheck(self.bindings)
        return {"synthetic_only": True, "beatnet_chunk_max_abs_error": delta,
                "beatnet_reset_exact": True, "pf_batch_chunk_exact": True,
                "pf_synthetic_events": len(batch), "beat_this_silence_completed": True,
                "beatnet_parameters": sum(p.numel() for p in self.beatnet.parameters())}

    @staticmethod
    def validate_seconds(events, duration):
        import math
        values = list(map(float, events))
        if any(not math.isfinite(v) or not 0 <= v < duration for v in values) or any(b <= a for a, b in zip(values, values[1:])):
            raise ValueError("upstream emitted invalid source timestamps")
        return values

    def infer(self, name: str, audio: Path, frames: int) -> dict:
        import soundfile as sf
        import soxr
        np, torch = self.np, self.torch
        pcm, sr = sf.read(audio, dtype="float32", always_2d=True, frames=frames)
        if sr != 48000 or pcm.shape != (frames, 2) or not np.isfinite(pcm).all():
            raise ValueError("canonical model input geometry differs")
        mono = soxr.resample(pcm.mean(axis=1), sr, 22050, quality="HQ")
        self.reset()
        with torch.inference_mode():
            if name == "beatnet":
                features = self.front.process_audio(mono).T
                x = torch.from_numpy(features).unsqueeze(0)
                pred = self.beatnet(x)[0]
                probabilities = self.beatnet.final_pred(pred).cpu().numpy()[:2].T
                events = self.decoder().process(probabilities)
                beats, db = events[:, 0], events[events[:, 1] == 1, 0]
            elif name == "beat_this":
                beats, db = self.beat_this(mono, 22050)
            else:
                raise ValueError("unregistered model")
        beats = self.validate_seconds(beats, frames / 48000)
        db = self.validate_seconds(db, frames / 48000)
        beat_frames = [reference.rounded(v * 48000) for v in beats]
        db_frames = [reference.rounded(v * 48000) for v in db]
        reference.valid_events(beat_frames, frames)
        reference.valid_events(db_frames, frames)
        if not set(db_frames) <= set(beat_frames):
            raise ValueError("downbeats are not on the candidate beat sequence")
        return {"beat_seconds": beats, "downbeat_seconds": db, "beat_frames": beat_frames,
                "downbeat_frames": db_frames, "confidence": None, "input_sample_rate": 22050}


def timeout(_signal, _frame):
    raise TimeoutError("registered model per-source time cap exceeded")


def worker(assets: Path, inputs: Path | None, model: str | None, output: Path):
    reference.external(output)
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    models = Models(assets)
    preflight = models.preflight()
    reference.history.write_new(output / "preflight.json", preflight)
    if inputs is None:
        return preflight
    digest = reference.history.sha256_file(inputs)
    value = json.loads(inputs.read_text())
    if (set(value) != {"format", "protocol_sha256", "prepared_manifest_sha256", "audio_root", "tracks"}
        or value["format"] != reference.FORMAT or value["protocol_sha256"] != reference.PROTOCOL_SHA256
        or len(value["tracks"]) != 100):
        raise ValueError("exact label-free frozen inference inputs required")
    allowed = {"track", "file", "sha256", "requested_frames"}
    if any(set(row) != allowed or Path(row["file"]).name != row["file"] for row in value["tracks"]):
        raise ValueError("inference inputs contain labels or unsafe paths")
    reference.bind(inputs, digest, models.bindings)
    paths = []
    for row in value["tracks"]:
        path = Path(value["audio_root"]) / row["file"]
        reference.bind(path, row["sha256"], models.bindings)
        paths.append(path)
    signal.signal(signal.SIGALRM, timeout)
    reports = []
    for row, path in zip(value["tracks"], paths):
        begin = time.monotonic()
        signal.alarm(models.config["resources"]["max_model_seconds_per_source"])
        try:
            events = models.infer(model, path, row["requested_frames"])
        finally:
            signal.alarm(0)
        elapsed = time.monotonic() - begin
        rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        if elapsed > models.config["resources"]["max_model_seconds_per_source"] or rss > models.config["resources"]["max_host_rss_kib"]:
            raise ValueError("registered host resource cap exceeded")
        target = output / (row["track"] + ".json")
        reference.history.write_new(target, {"track": row["track"], "audio_sha256": row["sha256"],
            "model": model, "events": events, "wall_seconds": elapsed, "rss_peak_kib": rss})
        reports.append({"track": row["track"], "events_sha256": reference.history.sha256_file(target),
                        "wall_seconds": elapsed, "rss_peak_kib": rss})
        print(json.dumps({"model": model, "tracks_done": len(reports), "seconds": round(elapsed, 3)}), flush=True)
    reference.recheck(models.bindings)
    reference.history.write_new(output / "report.json", {"format": reference.FORMAT,
        "formal_acceptance": False, "model": model, "inputs_sha256": digest, "outputs": reports,
        "source_bindings": models.bindings, "wall_seconds": time.monotonic() - started,
        "dependency_freeze": subprocess.check_output([sys.executable, "-m", "pip", "freeze", "--all"], text=True)})
    return {"model": model, "tracks": len(reports)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--inputs", type=Path)
    parser.add_argument("--model", choices=("beatnet", "beat_this"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if (args.inputs is None) != (args.model is None):
        parser.error("model and label-free inputs must be supplied together")
    try:
        result = worker(args.assets, args.inputs, args.model, args.output)
    except Exception as exc:
        if args.output.is_dir():
            reference.history.write_new(args.output / "failure.json", {"type": type(exc).__name__, "reason": str(exc),
                "accuracy_scored": False, "formal_acceptance": False})
        raise
    print(json.dumps(result))


if __name__ == "__main__":
    main()
