#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIRECTORY = ROOT / "whitakers-words/poc/compact-db"
sys.path.insert(0, str(SCRIPT_DIRECTORY))
SPEC = importlib.util.spec_from_file_location(
    "build_variable_width_appendix_poc",
    SCRIPT_DIRECTORY / "build_variable_width_appendix_poc.py",
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load variable-width appendix PoC builder")
POC = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = POC
SPEC.loader.exec_module(POC)


def candidate(*families: str) -> dict:
    return {
        "ascii_lemma": "alpha",
        "part_of_speech": "NOUN",
        "proper": False,
        "genders": ["n"],
        "witnesses": [
            {
                "source": family,
                "source_family": family,
                "source_entry_id": f"{family}:alpha",
                "morphology_key": f"{family}:model",
            }
            for family in families
        ],
    }


def extracted() -> object:
    return POC.ExtractedLexeme(
        candidate=candidate("lewis", "latin-german"),
        lemma="alpha",
        part="NOUN",
        part_code=POC.u16.PART_CODES["NOUN"],
        proper=False,
        gender=POC.u16.GENDERS["n"],
        declension_or_conjugation=2,
        variant=1,
        stems=("alpha", "", "", ""),
        method="synthetic",
        source_tier="single-primary",
        source_rank=2,
    )


class VariableWidthAppendixPocTest(unittest.TestCase):
    def test_source_policy_never_promotes_auxiliary_only_entry(self) -> None:
        self.assertEqual(
            POC.source_policy(candidate("lewis", "gaffiot")),
            (0, "lewis-short+gaffiot"),
        )
        self.assertEqual(
            POC.source_policy(candidate("lewis", "faria")),
            (1, "primary+faria-scan"),
        )
        self.assertEqual(
            POC.source_policy(candidate("lewis")), (2, "single-primary")
        )
        self.assertEqual(
            POC.source_policy(candidate("faria")), (3, "faria-scan-only")
        )
        self.assertIsNone(POC.source_policy(candidate("latin-german")))

    def test_fixed_record_round_trip_for_all_profiles(self) -> None:
        for width in POC.SUPPORTED_WIDTHS:
            widths = (width, 2, 3)
            values = ((1 << width) - 1, 3, 7)
            encoded = POC.encode_fixed_records(
                [[(value, bits) for value, bits in zip(values, widths)]]
            )
            self.assertEqual(len(encoded), (width + 5 + 7) // 8)
            self.assertEqual(POC.decode_fixed_record(encoded, 0, widths), values)

    def test_u16_reference_limit_does_not_apply_to_u18(self) -> None:
        item = extracted()
        arguments = {
            "extracted": [item],
            "base_pool": {"": 0, "alpha": 1},
            "base_values": ["", "alpha"],
            "base_lexemes": 1,
            "uniques": 0,
            "base_references": (1 << 16) - 1,
        }
        selected16, _, _, counts16 = POC.select_for_width(16, **arguments)
        selected18, _, _, counts18 = POC.select_for_width(18, **arguments)
        self.assertEqual(selected16, [])
        self.assertEqual(counts16["skipped_reference_capacity"], 1)
        self.assertEqual(selected18, [item])
        self.assertFalse(counts18)

    def test_image_verifier_checks_payload_integrity(self) -> None:
        item = extracted()
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            report = POC.encode_profile(
                root,
                18,
                [item],
                {"": 0, "alpha": 1},
                ["", "alpha"],
                [0] * POC.STEM_BUCKETS,
                base_lexemes=1,
                uniques=0,
                base_references=0,
                base_strings=2,
                capacity_counts=POC.Counter(),
            )
            image = Path(report["binary"]["path"])
            inspected = POC.inspect_image(image)
            self.assertEqual(inspected["id_bits"], 18)
            self.assertTrue(report["binary"]["verified"])

            damaged = bytearray(image.read_bytes())
            damaged[-1] ^= 1
            image.write_bytes(damaged)
            with self.assertRaisesRegex(POC.ProbeError, "CRC"):
                POC.inspect_image(image)


if __name__ == "__main__":
    unittest.main()
