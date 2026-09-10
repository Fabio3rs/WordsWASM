#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (
    ROOT
    / "whitakers-words/poc/compact-db/validate_grammar_specializations.py"
)
SPEC = importlib.util.spec_from_file_location(
    "validate_grammar_specializations", SCRIPT
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load grammar-specialization validator")
VALIDATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


EXPECTED_IDS = {
    "abbreviation.period-roman-conflict",
    "assessment.future-active-periphrastic-voice",
    "assessment.impersonal-number",
    "compound.finite-sum",
    "compound.infinitive-esse",
    "compound.infinitive-fuisse",
    "compound.supine-iri",
    "rewrite.syncope",
}


class GrammarSpecializationsTest(unittest.TestCase):
    def test_repository_manifest_is_complete_and_deterministic(self) -> None:
        path = ROOT / "whitakers-words/GRAMMAR_SPECIALIZATIONS.jsonl"
        records = VALIDATOR.load_manifest(path)
        VALIDATOR.validate_manifest(ROOT, records)
        schema = json.loads(
            (ROOT / "schemas/grammar-specialization-v1.schema.json").read_text()
        )
        validator = jsonschema.Draft202012Validator(schema)
        for record in records:
            validator.validate(record)

        self.assertEqual({record["id"] for record in records}, EXPECTED_IDS)
        self.assertTrue(all(record["witnesses"] for record in records))
        self.assertTrue(all(record["implementation"] for record in records))

        first = VALIDATOR.compile_trace(ROOT, records)
        second = VALIDATOR.compile_trace(ROOT, records)
        self.assertEqual(first, second)
        report = json.loads(first)
        self.assertEqual(report["schema"], "words.grammar-trace")
        self.assertEqual(report["schemaVersion"], 1)
        self.assertEqual(report["counts"]["specializations"], len(records))
        by_id = {record["id"]: record for record in records}
        self.assertEqual(
            by_id["assessment.impersonal-number"]["compatibility"],
            "differentially-equal",
        )
        self.assertEqual(
            by_id["assessment.impersonal-number"]["grammarStatus"],
            "legacy-bug",
        )

    def test_duplicate_identity_is_rejected(self) -> None:
        path = ROOT / "whitakers-words/GRAMMAR_SPECIALIZATIONS.jsonl"
        records = VALIDATOR.load_manifest(path)
        with self.assertRaisesRegex(ValueError, "duplicate specialization"):
            VALIDATOR.validate_manifest(ROOT, records + [records[0]])

    def test_dataset_sidecar_binds_profile_and_content_digest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory)
            source = bytearray(40)
            source[0] = 1
            source[4:8] = (1).to_bytes(4, "little")
            source[8:12] = (1).to_bytes(4, "little")
            source[12:15] = bytes((1, 1, 1))
            source[20:24] = (1).to_bytes(4, "little")
            source[24:28] = (1).to_bytes(4, "little")
            source[28] = ord("a")
            source[37] = 1
            source_path = fixture / "whitakers-words/INFLECTS.SEC"
            source_path.parent.mkdir()
            source_path.write_bytes(source)

            morphology = 1 | (1 << 3) | (1 << 5)
            packed = 1 | (0x11 << 4) | (morphology << 12) | (1 << 43)
            image = bytearray(78)
            image[:8] = VALIDATOR.WWDB_MAGIC
            image[12:16] = (40).to_bytes(4, "little")
            image[16:20] = (1).to_bytes(4, "little")
            image[20:24] = (2).to_bytes(4, "little")
            image[40:44] = (7).to_bytes(4, "little")
            image[44:48] = (1).to_bytes(4, "little")
            image[48:56] = (72).to_bytes(8, "little")
            image[56:64] = (6).to_bytes(8, "little")
            image[64:68] = (1).to_bytes(4, "little")
            image[68:72] = (6).to_bytes(4, "little")
            image[72:78] = packed.to_bytes(6, "little")
            database = fixture / "fixture.wwdb"
            database.write_bytes(image)
            report = json.loads(
                VALIDATOR.compile_trace(fixture, [], database, "dense")
            )
            image[72] ^= 1
            database.write_bytes(image)
            with self.assertRaisesRegex(ValueError, "transport mismatch"):
                VALIDATOR.compile_trace(fixture, [], database, "dense")
        self.assertEqual(report["dataset"]["profile"], "dense")
        self.assertEqual(report["dataset"]["bytes"], 78)
        self.assertRegex(report["dataset"]["digest"], r"^sha256:[0-9a-f]{64}$")
        self.assertEqual(report["inflections"]["count"], 1)
        self.assertEqual(report["inflections"]["records"][0]["ruleId"], 0)
        self.assertEqual(
            report["inflections"]["records"][0]["source"]["ending"], "a"
        )
        self.assertEqual(
            report["inflections"]["records"][0]["packed"]["usedBits"], 47
        )

    def test_missing_implementation_source_is_rejected(self) -> None:
        path = ROOT / "whitakers-words/GRAMMAR_SPECIALIZATIONS.jsonl"
        records = VALIDATOR.load_manifest(path)
        records[0]["implementation"][0]["path"] = "src/does-not-exist.cpp"
        with self.assertRaisesRegex(ValueError, "implementation source"):
            VALIDATOR.validate_manifest(ROOT, records)


if __name__ == "__main__":
    unittest.main()
