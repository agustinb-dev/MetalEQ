#!/usr/bin/env python3
"""Generate the measured family targets for MetalEQ.

Reads the profiler's family aggregates (Docs/GuitarStems/families.json) and
writes two artefacts:

  * Source/dsp/MetalEqToneTargetsMeasured.inc - two ToneTarget entries
    included directly in the constexpr kTargets table;
  * Tools/SpectrumProfiler/fixtures/measured_targets_expected.h - pinned
    feature values for the C++ drift test.

Band policy (v3.3):
  * 700 Hz - 5.6 kHz: measured family median, rounded to 0.5 dB. Tolerance
    and weight follow the inter-stem spread, so a family whose members
    disagree gets a wider deadband and less influence.
  * 9.5 kHz: engineering estimate (-6 dB) with weight 0.4. Every source stem
    is a lossy MP3 transcode whose 9.5 kHz band is codec-truncated
    (-30..-49 dB), so this band cannot be calibrated from the stems.
  * Classic thrash <= 350 Hz: engineering estimate. All three thrash stems
    are AI-separated and their sub-100 Hz content is kick/bass bleed; using
    the measured values would teach the analyzer to boost rumble.
  * Richardson metalcore <= 350 Hz: the clean official BFMV stem only
    (low anchor), because the AILD stems are leakage-corrupted there.

Usage:
    python3 Tools/SpectrumProfiler/generate_targets.py
    python3 Tools/SpectrumProfiler/generate_targets.py --check
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
FAMILIES_PATH = REPO_ROOT / "Docs" / "GuitarStems" / "families.json"
HEADER_PATH = REPO_ROOT / "Source" / "dsp" / "MetalEqToneTargets.h"
INCLUDE_PATH = REPO_ROOT / "Source" / "dsp" / "MetalEqToneTargetsMeasured.inc"
EXPECTED_PATH = REPO_ROOT / "Tools" / "SpectrumProfiler" / "fixtures" / "measured_targets_expected.h"

PINNED_TOLERANCE_DB = 0.5

# Thrash lows cannot be taken from the stems (separation leakage). These are
# the consensus of the existing thrash album estimates (Black Album, Rust in
# Peace, ...And Justice for All), not measurements.
THRASH_LOW_FALLBACK = {90.0: -2.5, 175.0: -0.5, 350.0: -1.5}

MEASURED_FIZZ_ESTIMATE_DB = -6.0

MAX_CORRECTION = {
    90.0: 3.0,
    175.0: 3.0,
    350.0: 3.0,
    700.0: 3.0,
    1400.0: 3.0,
    2800.0: 2.5,
    4000.0: 2.5,
    5600.0: 2.5,
    9500.0: 2.0,
}

TARGETS = [
    {
        "id": "genre-thrash-early-80s",
        "label": "Classic Thrash — '84-'86 Era",
        "shortLabel": "Classic Thrash",
        "category": "Category::genre",
        "family": "classic-thrash",
        "albumPeriod": "Metallica, Master of Puppets (1986) and Ride the Lightning (1984); "
                       "Megadeth, Peace Sells… But Who's Buying? (1986)",
        "objective": "A measured mid-'80s thrash destination: palm-mute weight without sub rumble, "
                     "a present 2.8-4 kHz attack and a controlled top. Built from isolated guitar "
                     "stems rather than interview-derived estimates.",
        "limitations": "The three thrash stems are AI-separated and their sub-100 Hz content is "
                       "contaminated by kick/bass bleed, so the 90-350 Hz values are engineering "
                       "estimates; the 700 Hz-5.6 kHz medians are measured with up to 9 dB spread "
                       "between stems. Lossy MP3 sources cannot calibrate 9.5 kHz fizz, which stays "
                       "an estimate with low weight. A family destination, not a claim about any one album.",
        "measuredFromStems": "Metallica - Master of Puppets (guitar stem); Metallica - For Whom the "
                             "Bell Tolls (guitar stem); Megadeth - The Conjuring (guitar stem)",
        "logic": [
            (6, "Flemming Rasmussen produced Master of Puppets; the measured MOP stem belongs to this family.", "documented"),
            (33, "Master of Puppets (1986) is one of the reference recordings for this period.", "documented"),
            (28, "Megadeth's thrash catalogue anchors this family; the measured Conjuring stem is from Peace Sells (1986), not Rust in Peace.", "documented"),
        ],
    },
    {
        "id": "genre-richardson-metalcore",
        "label": "Richardson-Era Metalcore",
        "shortLabel": "Richardson Metalcore",
        "category": "Category::genre",
        "family": "richardson-metalcore",
        "albumPeriod": "Bullet for My Valentine, Waking the Demon (2008); As I Lay Dying, "
                       "The Darkest Nights (2005) / My Own Grave (2019)",
        "objective": "A measured 2000s Colin Richardson metalcore destination: tight, controlled lows, "
                     "scooped low-mids, strong 2.8-4 kHz attack and a darker top than the classic-thrash family.",
        "limitations": "Low bands are anchored on the clean official Waking the Demon stem; the AILD "
                       "stems are AI-separated and contribute only at 350 Hz and above. The measured "
                       "AILD recordings span 2005-2019. The 9.5 kHz band is codec-truncated in every "
                       "stem and remains an engineering estimate with low weight.",
        "measuredFromStems": "Bullet for My Valentine - Waking the Demon (official guitar master); "
                             "As I Lay Dying - The Darkest Nights (isolated guitars); "
                             "As I Lay Dying - My Own Grave (guitars only)",
        "logic": [
            (12, "Colin Richardson's documented approach to modern metal guitar defines this family.", "documented"),
            (11, "BFMV's guitar approach and the clean Waking the Demon stem anchor the family.", "documented"),
            (24, "The Poison (2005) is the earlier BFMV reference; the measured WTD stem is from 2008.", "documented"),
            (26, "An Ocean Between Us (2007) is the AILD reference; the measured AILD stems span other records.", "documented"),
        ],
    },
]


def parse_features() -> list[dict]:
    text = HEADER_PATH.read_text(encoding="utf-8")
    start = text.index("kFeatures")
    block = text[start : text.index("}};", start)]
    pattern = re.compile(
        r'\{\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"'
    )
    features = [
        {"centre": float(m.group(1)), "label": m.group(4), "short": m.group(5)}
        for m in pattern.finditer(block)
    ]
    if len(features) != 9:
        raise SystemExit(f"Expected 9 features in {HEADER_PATH}, parsed {len(features)}")
    return features


def round_to(value: float, step: float) -> float:
    return round(value / step) * step


def db_text(value: float) -> str:
    if abs(value) < 0.05:
        return "0.0"
    return f"{value:+.1f}"


def target_confidence(family: dict) -> float:
    spreads = [
        band["spreadDb"]
        for band in family["bands"]
        if 700.0 <= band["centreHz"] < 9500.0
    ]
    mean_spread = sum(spreads) / len(spreads) if spreads else 0.0
    return round(min(0.75, max(0.60, 0.80 - 0.05 * mean_spread)), 2)


def features_for(spec: dict, family: dict, features: list[dict]) -> list[dict]:
    by_centre = {round(band["centreHz"]): band for band in family["bands"]}
    result = []

    for feature in features:
        centre = round(feature["centre"])
        band = by_centre[centre]
        anchored = "low anchor" in (band.get("note") or "")
        spread = float(band["spreadDb"])

        if centre in THRASH_LOW_FALLBACK and spec["family"] == "classic-thrash":
            result.append(
                {
                    "centre": centre,
                    "db": THRASH_LOW_FALLBACK[centre],
                    "tolerance": 1.0,
                    "weight": 0.5,
                    "max": MAX_CORRECTION[centre],
                    "evidence": "interpretation",
                    "rationale": "kThrashLowEstimate",
                }
            )
            continue

        if centre == 9500.0:
            result.append(
                {
                    "centre": centre,
                    "db": MEASURED_FIZZ_ESTIMATE_DB,
                    "tolerance": 1.5,
                    "weight": 0.4,
                    "max": MAX_CORRECTION[centre],
                    "evidence": "interpretation",
                    "rationale": "kMeasuredFizzEstimate",
                }
            )
            continue

        if anchored:
            tolerance, weight = 0.75, 0.8
            rationale = "kMeasuredLowAnchor"
        else:
            tolerance = min(2.0, max(0.75, round_to(0.75 + 0.10 * spread, 0.25)))
            weight = round_to(min(0.9, max(0.55, 0.9 - 0.04 * spread)), 0.05)
            rationale = "kMeasuredFamily"

        result.append(
            {
                "centre": centre,
                "db": round_to(float(band["medianRelDb"]), 0.5),
                "tolerance": tolerance,
                "weight": weight,
                "max": MAX_CORRECTION[centre],
                "evidence": "documented",
                "rationale": rationale,
            }
        )

    return result


def render_include(families: dict, features: list[dict]) -> str:
    lines = [
        "// Generated by Tools/SpectrumProfiler/generate_targets.py.",
        "// Source data: Docs/GuitarStems/families.json. Do not edit by hand.",
        "// Regenerate with: python3 Tools/SpectrumProfiler/generate_targets.py",
        "",
    ]

    rendered_targets = []
    for spec in TARGETS:
        family = families[spec["family"]]
        confidence = target_confidence(family)
        entries = features_for(spec, family, features)

        block = [
            "    {",
            f'        "{spec["id"]}",',
            f'        "{spec["label"]}",',
            f'        "{spec["shortLabel"]}",',
            f'        {spec["category"]},',
            f'        "{spec["albumPeriod"]}",',
            f'        "{spec["objective"]}",',
            f'        "{spec["limitations"]}",',
            f"        {confidence:.2f},",
            "        {{",
        ]
        for entry in entries:
            block.append(
                "            f ("
                f'{db_text(entry["db"])}, {entry["tolerance"]:.2f}, '
                f'{entry["weight"]:.2f}, {entry["max"]:.1f}, '
                f'Evidence::{entry["evidence"]}, {entry["rationale"]}),'
            )
        block.append("        }},")
        block.append("        {{")
        for source_index, claim, evidence in spec["logic"]:
            block.append(f'            src ({source_index}, "{claim}", Evidence::{evidence}),')
        block.append("        }},")
        block.append(f'        {len(spec["logic"])},')
        block.append(f'        "{spec["measuredFromStems"]}"')
        block.append("    }")
        rendered_targets.append("\n".join(block))

    lines.append(",\n".join(rendered_targets))
    lines.append("")
    return "\n".join(lines)


def render_expected(features: list[dict], families: dict) -> str:
    ids = [spec["id"] for spec in TARGETS]
    rows = []
    for spec in TARGETS:
        values = features_for(spec, families[spec["family"]], features)
        rows.append("    {{ " + ", ".join(f"{v['db']:.2f}" for v in values) + " }}")

    lines = [
        "// Generated by Tools/SpectrumProfiler/generate_targets.py.",
        "// Pinned expected values for the measured family targets.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "",
        "namespace metaleq::test::fixture",
        "{",
        f"inline constexpr std::size_t kMeasuredTargetCount = {len(ids)};",
        f"inline constexpr double kMeasuredPinnedToleranceDb = {PINNED_TOLERANCE_DB:.2f};",
        "",
        "inline constexpr std::array<const char*, "
        f"{len(ids)}> kMeasuredTargetIds {{ "
        + ", ".join(f'"{value}"' for value in ids)
        + " };",
        "",
        "inline constexpr std::array<std::array<double, "
        f"{len(features)}>, {len(rows)}> kMeasuredTargetExpectedDb {{{{",
        ",\n".join(rows),
        "}};",
        "",
        "} // namespace metaleq::test::fixture",
        "",
    ]
    return "\n".join(lines)


def write_or_check(path: Path, content: str, check: bool) -> bool:
    if check:
        if not path.exists():
            print(f"MISSING: {path}", file=sys.stderr)
            return False
        if path.read_text(encoding="utf-8") != content:
            print(f"MISMATCH: {path}", file=sys.stderr)
            return False
        print(f"OK: {path}")
        return True

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print(f"Wrote {path}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate measured MetalEQ family targets")
    parser.add_argument("--check", action="store_true", help="verify generated files are current")
    args = parser.parse_args()

    families = json.loads(FAMILIES_PATH.read_text(encoding="utf-8"))
    features = parse_features()

    include_text = render_include(families, features)
    expected_text = render_expected(features, families)

    ok = write_or_check(INCLUDE_PATH, include_text, args.check)
    ok = write_or_check(EXPECTED_PATH, expected_text, args.check) and ok

    if args.check and not ok:
        print("Run: python3 Tools/SpectrumProfiler/generate_targets.py", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
