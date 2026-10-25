#!/usr/bin/env python3
"""MetalEQ SpectrumProfiler.

Offline mirror of the analyzer input path (MetalEqToneTargets.cpp
`analyseInput()`): 8192-point Hann-windowed FFT, 50 % hop, per-frame silence
gate at mean square < 1e-8, linear power averaging, and the eight (later nine)
broad feature bands expressed in dB relative to the 500-2000 Hz reference.

The feature table is parsed from Source/dsp/MetalEqToneTargets.h so the plugin
remains the single source of truth. Output is deterministic JSON under
Docs/GuitarStems/ plus a C++ fixture header for the cross-language equivalence
test.

Usage (from the repository root, ffmpeg/ffprobe on PATH):

    python3 Tools/SpectrumProfiler/profile_stems.py --out Docs/GuitarStems
    python3 Tools/SpectrumProfiler/profile_stems.py --check
    python3 Tools/SpectrumProfiler/profile_stems.py --generate-fixture
    python3 Tools/SpectrumProfiler/profile_stems.py --self-test

The exact rendering of the analyzer is intentionally slow in pure Python; the
stems are therefore sampled evenly across the song (--frames, default 200).
The synthetic equivalence fixture uses --frames 0 (every hop frame).
"""

from __future__ import annotations

import argparse
import array
import cmath
import json
import math
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HEADER_PATH = REPO_ROOT / "Source" / "dsp" / "MetalEqToneTargets.h"
DEFAULT_OUT = REPO_ROOT / "Docs" / "GuitarStems"

FFT_SIZE = 8192
HOP = FFT_SIZE // 2
SILENCE_MEAN_SQUARE = 1.0e-8
REFERENCE_HZ = (500.0, 2000.0)
FIXTURE_RATE = 48000.0
FIXTURE_SECONDS = 8.0
FIXTURE_BANDS_TOLERANCE_DB = 0.25

# Deterministic provenance for the stems bundled in Docs/GuitarOnlyStems/.
# quality: clean = official master track; low-indep = AI separation;
#          unknown = separation quality undocumented.
STEMS = [
    {
        "id": "bfmv-waking-the-demon",
        "family": "richardson-metalcore",
        "quality": "clean",
        "path": "Docs/GuitarOnlyStems/Waking The Demon/Waking The Demon (GUITAR MASTER TRACK).mp3",
    },
    {
        "id": "aild-my-own-grave",
        "family": "richardson-metalcore",
        "quality": "low-indep",
        "path": "Docs/GuitarOnlyStems/As I Lay Dying/My Own Grave (low quality)/As I Lay Dying - My Own Grave (Guitars Only).mp3",
    },
    {
        "id": "aild-the-darkest-nights",
        "family": "richardson-metalcore",
        "quality": "low-indep",
        "path": "Docs/GuitarOnlyStems/As I Lay Dying/The Darkest Nights (Low Quality)/As I Lay Dying - The Darkest Nights [Isolated Guitars Track].mp3",
    },
    {
        "id": "megadeth-the-conjuring",
        "family": "classic-thrash",
        "quality": "unknown",
        "path": "Docs/GuitarOnlyStems/Megadeth/The Conjuring/Megadeth - The Conjuring (Guitar Track).mp3",
    },
    {
        "id": "metallica-master-of-puppets",
        "family": "classic-thrash",
        "quality": "unknown",
        "path": "Docs/GuitarOnlyStems/Metallica/Master Of Puppets/Metallica - Master Of Puppets (Guitars Only) [HD].mp3",
    },
    {
        "id": "metallica-for-whom-the-bell-tolls",
        "family": "classic-thrash",
        "quality": "unknown",
        "path": "Docs/GuitarOnlyStems/Metallica/For Whom The Bell Tolls/For Whom the Bell Tolls (Guitar Track).mp3",
    },
]

# Family aggregation rule: the Richardson family's low bands are anchored on
# the clean official stem instead of the leakage-corrupted AI-separated ones.
FAMILY_LOW_ANCHOR = {
    "richardson-metalcore": {"maxCentreHz": 350.0, "stem": "bfmv-waking-the-demon"},
}

FAMILY_TITLES = {
    "classic-thrash": "Classic Thrash — '84-'86 Era",
    "richardson-metalcore": "Richardson-Era Metalcore",
}


# ---------------------------------------------------------------------------
# Header parsing: keep the analyser bands in sync with the plugin.
# ---------------------------------------------------------------------------
def parse_features() -> list[dict]:
    text = HEADER_PATH.read_text(encoding="utf-8")
    start = text.index("kFeatures")
    block = text[start : text.index("}};", start)]
    pattern = re.compile(
        r'\{\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"'
    )
    features = []
    for match in pattern.finditer(block):
        features.append(
            {
                "centre": float(match.group(1)),
                "low": float(match.group(2)),
                "high": float(match.group(3)),
                "label": match.group(4),
                "short": match.group(5),
            }
        )
    if not features:
        raise SystemExit(f"No features parsed from {HEADER_PATH}")
    return features


# ---------------------------------------------------------------------------
# FFT: iterative radix-2, identical layout to metaleq::detail::fftRadix2.
# ---------------------------------------------------------------------------
def make_fft_plan(n: int):
    twiddles = [cmath.exp(-2j * math.pi * k / n) for k in range(n // 2)]
    return twiddles


def fft_inplace(a: list[complex], twiddles: list[complex]) -> None:
    n = len(a)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    length = 2
    while length <= n:
        half = length >> 1
        step = n // length
        for start in range(0, n, length):
            k = 0
            for m in range(start, start + half):
                t = a[m + half] * twiddles[k]
                u = a[m]
                a[m] = u + t
                a[m + half] = u - t
                k += step
        length <<= 1


def band_power_db(power: list[float], low: float, high: float, bin_hz: float, nyquist: float) -> float:
    low = max(1.0, low)
    high = min(high, nyquist * 0.95)
    if high <= low:
        return -160.0
    lo = max(1, math.ceil(low / bin_hz))
    hi = min(len(power) - 1, math.floor(high / bin_hz))
    if hi < lo:
        nearest = max(1, min(len(power) - 1, round(low / bin_hz)))
        return 10.0 * math.log10(power[nearest] + 1.0e-20)
    return 10.0 * math.log10(sum(power[lo : hi + 1]) / (hi - lo + 1) + 1.0e-20)


def analyse(samples: array.array, sample_rate: float, features: list[dict], max_frames: int) -> dict:
    total_frames = max(0, (len(samples) - FFT_SIZE) // HOP + 1)
    step = 1
    if max_frames > 0 and total_frames > max_frames:
        step = max(1, total_frames // max_frames)

    bin_hz = sample_rate / FFT_SIZE
    nyquist = sample_rate * 0.5
    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (FFT_SIZE - 1)) for i in range(FFT_SIZE)]
    twiddles = make_fft_plan(FFT_SIZE)

    power = [0.0] * (FFT_SIZE // 2 + 1)
    frames = 0
    skipped = 0
    total_square = 0.0
    per_frame_rel: list[list[float]] = []

    for frame_index in range(0, total_frames, step):
        offset = frame_index * HOP
        frame = samples[offset : offset + FFT_SIZE]
        frame_square = 0.0
        spec = [0j] * FFT_SIZE
        for i, sample in enumerate(frame):
            frame_square += sample * sample
            spec[i] = sample * window[i]
        mean_square = frame_square / FFT_SIZE
        if mean_square < SILENCE_MEAN_SQUARE:
            skipped += 1
            continue

        fft_inplace(spec, twiddles)
        frame_power = [0.0] * (FFT_SIZE // 2 + 1)
        for bin_index in range(1, FFT_SIZE // 2 + 1):
            value = spec[bin_index]
            p = value.real * value.real + value.imag * value.imag
            power[bin_index] += p
            frame_power[bin_index] = p

        ref = band_power_db(frame_power, *REFERENCE_HZ, bin_hz, nyquist)
        per_frame_rel.append(
            [band_power_db(frame_power, b["low"], b["high"], bin_hz, nyquist) - ref for b in features]
        )
        total_square += frame_square
        frames += 1

    if frames == 0:
        return {"valid": False, "framesUsed": 0, "framesSkipped": skipped}

    power = [p / frames for p in power]
    reference_db = band_power_db(power, *REFERENCE_HZ, bin_hz, nyquist)
    bands = []
    for index, feature in enumerate(features):
        mean_rel = band_power_db(power, feature["low"], feature["high"], bin_hz, nyquist) - reference_db
        values = sorted(row[index] for row in per_frame_rel)
        if values:
            median_rel = values[len(values) // 2]
            if len(values) % 2 == 0:
                median_rel = 0.5 * (values[len(values) // 2 - 1] + values[len(values) // 2])
            min_rel, max_rel = values[0], values[-1]
        else:
            median_rel = min_rel = max_rel = mean_rel
        bands.append(
            {
                "label": feature["short"],
                "centreHz": feature["centre"],
                "meanRelDb": round(mean_rel, 2),
                "medianRelDb": round(median_rel, 2),
                "minRelDb": round(min_rel, 2),
                "maxRelDb": round(max_rel, 2),
            }
        )

    sub60 = band_power_db(power, 15.0, 60.0, bin_hz, nyquist) - reference_db
    hf_floor = band_power_db(power, 10000.0, 16000.0, bin_hz, nyquist) - reference_db

    # Tilt in dB/octave across the centre bands 175-5600 Hz (median values).
    fit = [b for b in bands if 175.0 <= b["centreHz"] <= 5600.0]
    tilt = 0.0
    if len(fit) >= 2:
        xs = [math.log2(b["centreHz"]) for b in fit]
        ys = [b["medianRelDb"] for b in fit]
        n = len(fit)
        sx, sy = sum(xs), sum(ys)
        sxx = sum(x * x for x in xs)
        sxy = sum(x * y for x, y in zip(xs, ys))
        denominator = n * sxx - sx * sx
        if denominator:
            tilt = (n * sxy - sx * sy) / denominator

    rms = math.sqrt(total_square / (frames * FFT_SIZE))
    rms_db = 20.0 * math.log10(rms + 1.0e-20)

    return {
        "valid": True,
        "referenceDb": round(reference_db, 2),
        "rmsDb": round(rms_db, 2),
        "framesUsed": frames,
        "framesSkipped": skipped,
        "bands": bands,
        "sub60RelDb": round(sub60, 2),
        "hfFloorRelDb": round(hf_floor, 2),
        "tiltDbPerOct": round(tilt, 3),
    }


# ---------------------------------------------------------------------------
# Decoding.
# ---------------------------------------------------------------------------
def probe(path: Path) -> float:
    result = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "a:0",
            "-show_entries",
            "stream=sample_rate",
            "-of",
            "default=noprint_wrappers=1:nokey=1",
            str(path),
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    return float(result.stdout.strip().splitlines()[0])


def decode(path: Path, sample_rate: float) -> array.array:
    result = subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(path),
            "-ac",
            "1",
            "-ar",
            str(int(sample_rate)),
            "-f",
            "f32le",
            "-",
        ],
        capture_output=True,
        check=True,
    )
    samples = array.array("f")
    samples.frombytes(result.stdout)
    return samples


# ---------------------------------------------------------------------------
# Reports.
# ---------------------------------------------------------------------------
def profile_stem(stem: dict, features: list[dict], max_frames: int) -> dict:
    path = REPO_ROOT / stem["path"]
    if not path.exists():
        raise SystemExit(f"Missing stem: {path}")
    sample_rate = probe(path)
    samples = decode(path, sample_rate)
    analysis = analyse(samples, sample_rate, features, max_frames)
    analysis.update(
        {
            "id": stem["id"],
            "family": stem["family"],
            "quality": stem["quality"],
            "path": stem["path"],
            "sampleRate": int(sample_rate),
            "durationSeconds": round(len(samples) / sample_rate, 2),
        }
    )
    return analysis


def median(values: list[float]) -> float:
    ordered = sorted(values)
    n = len(ordered)
    if n == 0:
        return 0.0
    if n % 2:
        return ordered[n // 2]
    return 0.5 * (ordered[n // 2 - 1] + ordered[n // 2])


def aggregate_families(stems: list[dict], features: list[dict]) -> dict:
    families: dict[str, dict] = {}
    for stem in stems:
        families.setdefault(stem["family"], []).append(stem)

    output: dict[str, dict] = {}
    for family, members in sorted(families.items()):
        anchor = FAMILY_LOW_ANCHOR.get(family)
        bands = []
        for index, feature in enumerate(features):
            source_members = members
            note = None
            if anchor and feature["centre"] <= anchor["maxCentreHz"]:
                source_members = [m for m in members if m["id"] == anchor["stem"]] or members
                note = f"low anchor: {source_members[0]['id']}"
            bands.append(
                {
                    "label": feature["short"],
                    "centreHz": feature["centre"],
                    "medianRelDb": round(median([m["bands"][index]["medianRelDb"] for m in source_members]), 2),
                    "spreadDb": round(
                        max(m["bands"][index]["medianRelDb"] for m in source_members)
                        - min(m["bands"][index]["medianRelDb"] for m in source_members),
                        2,
                    ),
                    **({"note": note} if note else {}),
                }
            )
        output[family] = {
            "title": FAMILY_TITLES.get(family, family),
            "members": [m["id"] for m in members],
            "bands": bands,
        }
    return output


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def build_payload(stems: list[dict], features: list[dict], frame_limit: int) -> dict:
    return {
        "schema": 1,
        "analyzer": {
            "fftSize": FFT_SIZE,
            "hop": HOP,
            "window": "hann",
            "referenceHz": list(REFERENCE_HZ),
            "silenceMeanSquare": SILENCE_MEAN_SQUARE,
            "frameLimit": frame_limit,
            "features": [
                {"label": f["short"], "centreHz": f["centre"], "lowHz": f["low"], "highHz": f["high"]}
                for f in features
            ],
        },
        "tool": "Tools/SpectrumProfiler/profile_stems.py",
        "stems": stems,
        "families": aggregate_families(stems, features),
    }


# ---------------------------------------------------------------------------
# Synthetic equivalence signal: defined identically in
# Tests/MetalEqToneTargetTests.cpp (testAnalyzerProfilerEquivalence).
# ---------------------------------------------------------------------------
def synthetic_signal() -> array.array:
    count = int(FIXTURE_RATE * FIXTURE_SECONDS)
    samples = array.array("f", bytes(4 * count))
    two_pi = 2.0 * math.pi
    for i in range(count):
        t = i / FIXTURE_RATE
        noise = ((1103515245 * i + 12345) >> 16) & 0x7FFF
        value = (
            0.25 * math.sin(two_pi * 1000.0 * t)
            + 0.10 * math.sin(two_pi * 250.0 * t)
            + 0.06 * math.sin(two_pi * 4000.0 * t)
            + 0.02 * math.sin(two_pi * 9500.0 * t)
            + 0.01 * (noise / 32768.0 - 1.0)
        )
        samples[i] = value
    return samples


def fixture_values(features: list[dict]) -> list[float]:
    samples = synthetic_signal()
    analysis = analyse(samples, FIXTURE_RATE, features, max_frames=0)
    if not analysis["valid"]:
        raise SystemExit("Synthetic fixture produced no usable frames")
    return [b["meanRelDb"] for b in analysis["bands"]]


def write_fixtures(features: list[dict], values: list[float]) -> None:
    fixtures = REPO_ROOT / "Tools" / "SpectrumProfiler" / "fixtures"
    payload = {
        "description": "Synthetic deterministic buffer; see syncSyntheticSignal in the profiler and Tests/MetalEqToneTargetTests.cpp.",
        "sampleRate": FIXTURE_RATE,
        "seconds": FIXTURE_SECONDS,
        "fftSize": FFT_SIZE,
        "hop": HOP,
        "toleranceDb": FIXTURE_BANDS_TOLERANCE_DB,
        "labels": [f["short"] for f in features],
        "expectedMeanRelDb": values,
    }
    write_json(fixtures / "synthetic_expected.json", payload)

    header_lines = [
        "// Generated by Tools/SpectrumProfiler/profile_stems.py --generate-fixture.",
        "// Deterministic synthetic-buffer expectations for the analyzer equivalence test.",
        "#pragma once",
        "",
        "#include <array>",
        "",
        "namespace metaleq::test::fixture",
        "{",
        f"inline constexpr double kSampleRate = {FIXTURE_RATE:.1f};",
        f"inline constexpr double kSeconds = {FIXTURE_SECONDS:.1f};",
        f"inline constexpr double kToleranceDb = {FIXTURE_BANDS_TOLERANCE_DB:.2f};",
        "",
        "inline constexpr std::array<const char*, "
        f"{len(features)}> kLabels {{ "
        + ", ".join(f'"{f["short"]}"' for f in features)
        + " };",
        "",
        "inline constexpr std::array<double, "
        f"{len(values)}> kExpectedMeanRelDb {{ "
        + ", ".join(f"{v:.3f}" for v in values)
        + " };",
        "",
        "} // namespace metaleq::test::fixture",
        "",
    ]
    (fixtures / "synthetic_expected.h").write_text("\n".join(header_lines), encoding="utf-8")


# ---------------------------------------------------------------------------
# Commands.
# ---------------------------------------------------------------------------
def command_profile(args, features: list[dict]) -> int:
    stems = [profile_stem(stem, features, args.frames) for stem in STEMS]
    payload = build_payload(stems, features, args.frames)
    out_dir = Path(args.out)
    if not out_dir.is_absolute():
        out_dir = REPO_ROOT / out_dir
    write_json(out_dir / "stems.json", payload)
    for stem in stems:
        write_json(out_dir / f"{stem['id']}.json", stem)
    write_json(out_dir / "families.json", payload["families"])

    print(f"Profiled {len(stems)} stems -> {out_dir}")
    for stem in stems:
        bands = " ".join(f"{b['label']}:{b['medianRelDb']:+.1f}" for b in stem["bands"])
        print(f"  {stem['id']:<32} {bands}")
        print(
            f"    frames {stem['framesUsed']}  sub60 {stem['sub60RelDb']:+.1f}"
            f"  hfFloor {stem['hfFloorRelDb']:+.1f}  tilt {stem['tiltDbPerOct']:+.2f} dB/oct"
        )
    for family, aggregate in payload["families"].items():
        bands = " ".join(f"{b['label']}:{b['medianRelDb']:+.1f}" for b in aggregate["bands"])
        print(f"  family {family}: {bands}")
    return 0


def command_check(args, features: list[dict]) -> int:
    out_dir = Path(args.out)
    if not out_dir.is_absolute():
        out_dir = REPO_ROOT / out_dir
    previous = load_json(out_dir / "stems.json")
    stems = [profile_stem(stem, features, args.frames) for stem in STEMS]
    current = build_payload(stems, features, args.frames)

    problems = []
    old_by_id = {stem["id"]: stem for stem in previous["stems"]}
    for stem in current["stems"]:
        old = old_by_id.get(stem["id"])
        if old is None:
            problems.append(f"{stem['id']}: missing from previous run")
            continue
        for new_band, old_band in zip(stem["bands"], old["bands"]):
            delta = abs(new_band["medianRelDb"] - old_band["medianRelDb"])
            if delta > 0.05:
                problems.append(f"{stem['id']} {new_band['label']}: {delta:.3f} dB drift")
    if problems:
        for problem in problems:
            print(f"MISMATCH: {problem}", file=sys.stderr)
        return 1
    print(f"--check OK: {len(current['stems'])} stems match {out_dir / 'stems.json'}")
    return 0


def command_fixture(args, features: list[dict]) -> int:
    values = fixture_values(features)
    write_fixtures(features, values)
    print("Wrote fixtures/synthetic_expected.json and fixtures/synthetic_expected.h")
    for feature, value in zip(features, values):
        print(f"  {feature['short']:>5}: {value:+.2f} dB")
    return 0


def command_self_test(args, features: list[dict]) -> int:
    expected = load_json(REPO_ROOT / "Tools" / "SpectrumProfiler" / "fixtures" / "synthetic_expected.json")
    values = fixture_values(features)
    failures = 0
    for feature, value, label in zip(features, values, expected["labels"]):
        if feature["short"] != label:
            print(f"FAIL: band order changed: {feature['short']} vs {label}", file=sys.stderr)
            failures += 1
    for value, expected_value, label in zip(values, expected["expectedMeanRelDb"], expected["labels"]):
        if abs(value - expected_value) > expected["toleranceDb"]:
            print(
                f"FAIL: {label}: expected {expected_value:+.2f}, got {value:+.2f}",
                file=sys.stderr,
            )
            failures += 1
    if failures:
        return 1
    print(f"Self-test OK: {len(values)} bands within {expected['toleranceDb']} dB")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="MetalEQ spectrum profiler")
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="output directory")
    parser.add_argument(
        "--frames",
        type=int,
        default=200,
        help="maximum frames sampled per stem (0 = every hop frame; slow)",
    )
    parser.add_argument("--check", action="store_true", help="recompute and compare with stored JSON")
    parser.add_argument("--generate-fixture", action="store_true", help="regenerate the equivalence fixture")
    parser.add_argument("--self-test", action="store_true", help="verify the profiler against the fixture")
    args = parser.parse_args()

    features = parse_features()
    if args.check:
        return command_check(args, features)
    if args.generate_fixture:
        return command_fixture(args, features)
    if args.self_test:
        return command_self_test(args, features)
    return command_profile(args, features)


if __name__ == "__main__":
    sys.exit(main())
