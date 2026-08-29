#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/import_quantities.py"
SPEC = importlib.util.spec_from_file_location("import_quantities", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load quantity importer")
IMPORTER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = IMPORTER
SPEC.loader.exec_module(IMPORTER)


def source() -> dict[str, object]:
    return {
        "record": "source",
        "id": "fixture",
        "title": "Fixture",
        "artifact": "fixture.jsonl",
        "version": "1",
        "method": "grammar_reference",
        "reliability": "high",
    }


def evidence(
    evidence_id: str,
    marked: str,
    *,
    confidence: str = "confirmed",
    target: dict[str, object] | None = None,
) -> dict[str, object]:
    return {
        "record": "evidence",
        "id": evidence_id,
        "target": target or {"kind": "stem", "dictionary_entry": 1, "slot": 1},
        "base": "amo",
        "marked": marked,
        "source": "fixture",
        "locator": evidence_id,
        "witness": marked,
        "confidence": confidence,
        "label": "fixture quantity",
    }


class QuantityImportTest(unittest.TestCase):
    def compile(self, records: list[dict[str, object]]):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.jsonl"
            path.write_text(
                "".join(json.dumps(record, ensure_ascii=False) + "\n" for record in records),
                encoding="utf-8",
            )
            return IMPORTER.compile_evidence(path)

    def test_repository_microdata_is_generated_deterministically(self) -> None:
        result = IMPORTER.compile_evidence(ROOT / "whitakers-words/QUANTITY_EVIDENCE.jsonl")
        self.assertEqual(
            result.microdata,
            (ROOT / "whitakers-words/QUANTITIES.LAT").read_text(encoding="utf-8"),
        )
        report = json.loads(result.report)
        self.assertEqual(report["counts"]["promoted_targets"], 9)
        self.assertEqual(report["counts"]["deferred_evidence"], 2)

    def test_confirmed_evidence_merges_disjoint_positions(self) -> None:
        result = self.compile([source(), evidence("first", "āmo"), evidence("last", "amŏ")])
        self.assertEqual(len(result.promoted), 1)
        self.assertEqual(result.promoted[0].known, 0b101)
        self.assertEqual(result.promoted[0].long_vowel, 0b001)

    def test_probable_evidence_is_reported_but_not_promoted(self) -> None:
        result = self.compile([source(), evidence("deferred", "ămŏ", confidence="probable")])
        self.assertEqual(result.promoted, ())
        report = json.loads(result.report)
        self.assertEqual(report["counts"]["deferred_evidence"], 1)
        self.assertFalse(report["evidence"][0]["promoted"])

    def test_conflicting_confirmed_evidence_is_fatal(self) -> None:
        with self.assertRaisesRegex(IMPORTER.QuantityImportError, "quantity conflict"):
            self.compile([source(), evidence("long", "āmo"), evidence("short", "ămo")])

    def test_auxiliary_source_cannot_be_promoted_as_confirmed(self) -> None:
        auxiliary = source()
        auxiliary["reliability"] = "auxiliary"
        with self.assertRaisesRegex(IMPORTER.QuantityImportError, "auxiliary source"):
            self.compile([auxiliary, evidence("legacy", "āmo")])

    def test_rejects_quantity_on_consonant(self) -> None:
        with self.assertRaisesRegex(IMPORTER.QuantityImportError, "consonant"):
            self.compile([source(), evidence("bad", "am\u0304o")])


if __name__ == "__main__":
    unittest.main()
