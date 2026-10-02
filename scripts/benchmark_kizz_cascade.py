#!/usr/bin/env python3
"""Replay Kizz's detector/verifier cascade and estimate its runtime budget.

This is a desktop replay, not an ESP32 timing claim. It uses the same 16 kHz
micro-speech frontend and streaming tensor geometry as the firmware, then
measures the two-stage decision and the amount of verifier work avoided by the
cascade.
"""

from __future__ import annotations

import argparse
import json
import time
import wave
from pathlib import Path

import numpy as np
import tensorflow as tf
from pymicro_features import MicroFrontend


# The C MicroFrontend exposes roughly 0..666 unsigned feature values. The
# Python binding exposes the same features after the training repository's
# uint16 -> float conversion (roughly 0..26), so restore the C scale before
# applying the firmware quantizer.
FIRMWARE_FEATURE_SCALE = 25.6
FEATURE_QUANT_SCALE = 256
FEATURE_QUANT_DIVISOR = 666
FEATURES_PER_SLICE = 40
SAMPLES_PER_SLICE = 160
MODEL_STRIDE = 3
FRONTEND_WINDOW_MS = 30
FRONTEND_STEP_MS = 10
ORDERED_BASELINE_SCORE = 16.167534
ORDERED_LOG_SELF = -0.510825624
ORDERED_LOG_NEXT = -0.916290732


def distribution(values: list[float]) -> dict:
    """Return stable nearest-rank latency statistics for a bounded sample."""
    if not values:
        return {"count": 0, "p50": None, "p95": None, "p99": None, "max": None}
    ordered = sorted(float(value) for value in values)

    def nearest_rank(percentile: float) -> float:
        rank = max(1, int(np.ceil(percentile * len(ordered))))
        return ordered[rank - 1]

    return {
        "count": len(ordered),
        "p50": nearest_rank(0.50),
        "p95": nearest_rank(0.95),
        "p99": nearest_rank(0.99),
        "max": ordered[-1],
    }


def read_wav(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as wav:
        if wav.getframerate() != 16000 or wav.getnchannels() != 1 or wav.getsampwidth() != 2:
            raise ValueError(f"{path}: expected mono 16-bit 16 kHz WAV")
        return np.frombuffer(wav.readframes(wav.getnframes()), dtype=np.int16).copy()


def micro_features(samples: np.ndarray) -> np.ndarray:
    frontend = MicroFrontend()
    process = getattr(frontend, "process_samples", None) or frontend.ProcessSamples
    audio = samples.tobytes()
    offset = 0
    features = []
    while offset + SAMPLES_PER_SLICE * 2 <= len(audio):
        result = process(audio[offset : offset + SAMPLES_PER_SLICE * 2])
        offset += result.samples_read * 2
        if result.features:
            row = np.asarray(result.features, dtype=np.int32)
            if row.size != FEATURES_PER_SLICE:
                raise ValueError(f"unexpected frontend width {row.size}")
            features.append(row)
    return np.asarray(features, dtype=np.int32)


def timed_micro_features(samples: np.ndarray) -> tuple[np.ndarray, float]:
    started = time.perf_counter_ns()
    features = micro_features(samples)
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000.0
    return features, elapsed_ms


def quantize_features(features: np.ndarray) -> np.ndarray:
    raw_features = features.astype(np.float32) * FIRMWARE_FEATURE_SCALE
    value = (raw_features * FEATURE_QUANT_SCALE +
             FEATURE_QUANT_DIVISOR / 2) // FEATURE_QUANT_DIVISOR
    return np.clip(value - 128, -128, 127).astype(np.int8)


class StreamingModel:
    def __init__(self, path: Path):
        self.path = path
        self.interpreter = tf.lite.Interpreter(model_path=str(path), num_threads=1)
        self.interpreter.allocate_tensors()
        self.input = self.interpreter.get_input_details()[0]
        self.output = self.interpreter.get_output_details()[0]
        shape = tuple(int(v) for v in self.input["shape"])
        if shape != (1, MODEL_STRIDE, FEATURES_PER_SLICE):
            raise ValueError(f"{path}: unexpected input shape {shape}")
        if self.output["dtype"] != np.uint8:
            raise ValueError(f"{path}: expected uint8 output")

    def invoke(self, chunk: np.ndarray) -> tuple[np.ndarray, float]:
        self.interpreter.set_tensor(self.input["index"], chunk[None, ...])
        started = time.perf_counter_ns()
        self.interpreter.invoke()
        elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000.0
        return self.interpreter.get_tensor(self.output["index"]), elapsed_ms

    def scores(self, features: np.ndarray, stop_at: float | None = None) -> tuple[np.ndarray, float, int, list[float]]:
        quantized = quantize_features(features)
        chunks = len(quantized) // MODEL_STRIDE
        values = []
        invoke_ms = []
        started = time.perf_counter_ns()
        for index in range(chunks):
            chunk = quantized[index * MODEL_STRIDE : (index + 1) * MODEL_STRIDE]
            output, elapsed_invoke_ms = self.invoke(chunk)
            invoke_ms.append(elapsed_invoke_ms)
            score = float(output.reshape(-1)[0]) / 255.0
            values.append(score)
            if stop_at is not None and score >= stop_at:
                break
        elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000.0
        return np.asarray(values, dtype=np.float32), elapsed_ms, len(values), invoke_ms


class OrderedDetector(StreamingModel):
    """Python port of OrderedStateModel::perform_ordered_inference."""

    def __init__(self, path: Path):
        super().__init__(path)
        if tuple(self.output["shape"]) != (1, 1, 23):
            raise ValueError(f"{path}: expected ordered 1x1x23 output")
        self.output_scale = float(self.output["quantization"][0])
        self.output_zero_point = float(self.output["quantization"][1])

    @staticmethod
    def _logsumexp(values: np.ndarray) -> float:
        maximum = float(np.max(values))
        return maximum + float(np.log(np.exp(values - maximum).sum()))

    def scores(self, features: np.ndarray, stop_at: float | None = None) -> tuple[np.ndarray, float, int, np.ndarray, list[float]]:
        quantized = quantize_features(features)
        chunks = len(quantized) // MODEL_STRIDE
        scores = np.full(21, -np.inf, dtype=np.float64)
        completion = -np.inf
        probabilities = []
        completion_scores = []
        invoke_ms = []
        started = time.perf_counter_ns()
        for index in range(chunks):
            chunk = quantized[index * MODEL_STRIDE : (index + 1) * MODEL_STRIDE]
            output, elapsed_invoke_ms = self.invoke(chunk)
            invoke_ms.append(elapsed_invoke_ms)
            output = output.reshape(-1).astype(np.float64)
            logits = (output - self.output_zero_point) * self.output_scale
            log_normalizer = self._logsumexp(logits)
            rejection = self._logsumexp(logits[:2] - log_normalizer)
            emissions = logits[2:] - log_normalizer - rejection
            next_scores = np.full(21, -np.inf, dtype=np.float64)
            for state, emission in enumerate(emissions):
                best = emission if state == 0 else -np.inf
                if np.isfinite(scores[state]):
                    best = max(best, scores[state] + ORDERED_LOG_SELF + emission)
                if state > 0 and np.isfinite(scores[state - 1]):
                    best = max(best, scores[state - 1] + ORDERED_LOG_NEXT + emission)
                next_scores[state] = best
            scores = next_scores
            completion = float(scores[20])
            if np.isfinite(completion):
                shifted = completion - ORDERED_BASELINE_SCORE
                probability = (1.0 if shifted >= 40.0 else
                               0.0 if shifted <= -40.0 else
                               1.0 / (1.0 + np.exp(-shifted)))
            else:
                probability = 0.0
            probabilities.append(probability)
            completion_scores.append(completion)
            if stop_at is not None and completion >= stop_at:
                break
        elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000.0
        return (np.asarray(probabilities, dtype=np.float32), elapsed_ms,
                len(probabilities),
                np.asarray(completion_scores, dtype=np.float32), invoke_ms)


def candidate_clip(samples: np.ndarray, score_index: int, window_ms: int) -> np.ndarray:
    # Firmware snapshots the most recent three seconds before the detector
    # callback. Keep the same trailing-window shape in the replay.
    end = min(len(samples), (score_index + 1) * MODEL_STRIDE * 10 * 16)
    start = max(0, end - window_ms * 16)
    clip = samples[start:end]
    if len(clip) < window_ms * 16:
        clip = np.pad(clip, (window_ms * 16 - len(clip), 0))
    return clip


def run_case(detector_path: Path, verifier_path: Path, path: Path, args) -> dict:
    samples = read_wav(path)
    features, detector_frontend_ms = timed_micro_features(samples)
    if args.detector_kind == "ordered":
        detector = OrderedDetector(detector_path)
        (detector_scores, detector_ms, detector_invocations,
         completion_scores, detector_invoke_ms) = detector.scores(
             features, stop_at=ORDERED_BASELINE_SCORE)
        hits = np.flatnonzero(completion_scores >= ORDERED_BASELINE_SCORE)
    else:
        detector = StreamingModel(detector_path)
        (detector_scores, detector_ms, detector_invocations,
         detector_invoke_ms) = detector.scores(
             features, stop_at=args.detector_cutoff)
        completion_scores = np.asarray([], dtype=np.float32)
        hits = np.flatnonzero(detector_scores >= args.detector_cutoff)
    detector_index = int(hits[0]) if len(hits) else None
    verifier_ms = 0.0
    verifier_frontend_ms = 0.0
    verifier_invoke_ms = []
    verifier_score = None
    accepted = False
    if detector_index is not None:
        clip = candidate_clip(samples, detector_index, args.candidate_ms)
        verifier_features, verifier_frontend_ms = timed_micro_features(clip)
        # TensorFlow Lite's desktop interpreter does not expose the same
        # MicroResourceVariables reset primitive as the firmware wrapper. Use
        # a fresh interpreter per candidate to model that reset exactly; model
        # construction is outside the timed inference interval. The firmware
        # keeps this arena/interpreter resident and resets variables in place.
        verifier = StreamingModel(verifier_path)
        (verifier_scores, verifier_ms, verifier_invocations,
         verifier_invoke_ms) = verifier.scores(
             verifier_features, stop_at=args.verifier_cutoff)
        verifier_score = float(verifier_scores.max()) if len(verifier_scores) else 0.0
        accepted = verifier_score >= args.verifier_cutoff
    else:
        verifier_invocations = 0
    positive = "natural" in path.name or "live-miss" in path.name
    return {
        "file": str(path),
        "positive": positive,
        "seconds": len(samples) / 16000.0,
        "detector_outputs": int(len(detector_scores)),
        "detector_invocations": detector_invocations,
        "detector_peak_probability": float(detector_scores.max()) if len(detector_scores) else 0.0,
        "detector_peak_completion_score": (float(completion_scores.max())
                                           if len(completion_scores) else None),
        "detector_frontend_ms": detector_frontend_ms,
        "detector_ms": detector_ms,
        "detector_invoke_ms": detector_invoke_ms,
        "detector_triggered": detector_index is not None,
        "verifier_frontend_ms": verifier_frontend_ms,
        "verifier_ms": verifier_ms,
        "verifier_invoke_ms": verifier_invoke_ms,
        "verifier_invocations": verifier_invocations,
        "verifier_score": verifier_score,
        "accepted": accepted,
    }


def run_candidate_storm(verifier_path: Path, candidate_ms: int,
                        repetitions: int) -> dict:
    """Exercise the full, non-early-exit verifier workload repeatedly.

    This is deliberately a host workload bound. It proves invocation counts and
    catches regressions in the benchmark/model contract, but it does not stand
    in for ESP32-S3 timing or queue-loss telemetry.
    """
    samples = np.zeros(candidate_ms * 16, dtype=np.int16)
    candidate_total_ms = []
    frontend_ms = []
    score_loop_ms = []
    invoke_ms = []
    invocations = []
    verifier = StreamingModel(verifier_path)
    for _ in range(repetitions):
        started = time.perf_counter_ns()
        features, one_frontend_ms = timed_micro_features(samples)
        _, one_score_ms, one_invocations, one_invoke_ms = verifier.scores(
            features, stop_at=None)
        candidate_total_ms.append(
            (time.perf_counter_ns() - started) / 1_000_000.0)
        frontend_ms.append(one_frontend_ms)
        score_loop_ms.append(one_score_ms)
        invoke_ms.extend(one_invoke_ms)
        invocations.append(one_invocations)
    return {
        "contract": "host-only full-window verifier; early exit disabled",
        "runs": repetitions,
        "candidate_ms": candidate_ms,
        "expected_invocations_per_candidate": (
            max(0, 1 + (candidate_ms - FRONTEND_WINDOW_MS) // FRONTEND_STEP_MS)
            // MODEL_STRIDE),
        "observed_invocations_per_candidate": distribution(invocations),
        "candidate_total_ms": distribution(candidate_total_ms),
        "frontend_ms": distribution(frontend_ms),
        "score_loop_ms": distribution(score_loop_ms),
        "invoke_ms": distribution(invoke_ms),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--detector", type=Path, required=True)
    parser.add_argument("--verifier", type=Path, required=True)
    parser.add_argument("--audio-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--detector-cutoff", type=float, default=0.70)
    parser.add_argument("--detector-kind", choices=("scalar", "ordered"), default="scalar")
    parser.add_argument("--verifier-cutoff", type=float, default=167 / 255)
    parser.add_argument("--candidate-ms", type=int, default=3000)
    parser.add_argument("--candidate-storm-runs", type=int, default=20)
    args = parser.parse_args()
    if args.candidate_storm_runs < 1:
        parser.error("--candidate-storm-runs must be at least 1")

    cases = [run_case(args.detector, args.verifier, path, args)
             for path in sorted(args.audio_dir.glob("*.wav"))]
    positives = [case for case in cases if case["positive"]]
    negatives = [case for case in cases if not case["positive"]]
    triggers = sum(case["detector_triggered"] for case in cases)
    verifier_runs = sum(case["detector_triggered"] for case in cases)
    total_seconds = sum(case["seconds"] for case in cases)
    detector_invoke_ms = [value for case in cases
                          for value in case["detector_invoke_ms"]]
    verifier_invoke_ms = [value for case in cases
                          for value in case["verifier_invoke_ms"]]
    host_processing_ms = sum(
        case["detector_frontend_ms"] + case["detector_ms"] +
        case["verifier_frontend_ms"] + case["verifier_ms"]
        for case in cases)
    result = {
        "contract": {
            "frontend": "pymicro_features MicroFrontend, 16 kHz mono",
            "stream_stride_ms": 30,
            "verifier_resident": True,
            "desktop_verifier_reset": "fresh interpreter per candidate",
            "verifier_early_exit": True,
            "timing_scope": "host-only; never an ESP32-S3 timing claim",
            "detector_kind": args.detector_kind,
            "candidate_window_ms": args.candidate_ms,
            "detector_cutoff": args.detector_cutoff,
            "verifier_cutoff": args.verifier_cutoff,
        },
        "summary": {
            "clips": len(cases),
            "positive_clips": len(positives),
            "negative_clips": len(negatives),
            "detector_trigger_rate": triggers / len(cases) if cases else 0.0,
            "verifier_invocations": verifier_runs,
            "detector_model_invocations": sum(case["detector_invocations"] for case in cases),
            "verifier_model_invocations": sum(case["verifier_invocations"] for case in cases),
            "verifier_invocations_per_audio_hour": verifier_runs / (total_seconds / 3600.0) if total_seconds else 0.0,
            "detector_wall_ms": sum(case["detector_ms"] for case in cases),
            "verifier_wall_ms": sum(case["verifier_ms"] for case in cases),
            "cascade_accepts": sum(case["accepted"] for case in cases),
            "positive_recall": sum(case["accepted"] for case in positives) / len(positives) if positives else 0.0,
            "negative_accepts": sum(case["accepted"] for case in negatives),
            "host_processing_realtime_factor": (
                host_processing_ms / (total_seconds * 1000.0)
                if total_seconds else None),
            "detector_frontend_ms": distribution(
                [case["detector_frontend_ms"] for case in cases]),
            "detector_invoke_ms": distribution(detector_invoke_ms),
            "verifier_frontend_ms": distribution(
                [case["verifier_frontend_ms"] for case in cases
                 if case["detector_triggered"]]),
            "verifier_invoke_ms": distribution(verifier_invoke_ms),
        },
        "candidate_storm": run_candidate_storm(
            args.verifier, args.candidate_ms, args.candidate_storm_runs),
        "cases": cases,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result["summary"], indent=2))


if __name__ == "__main__":
    main()
