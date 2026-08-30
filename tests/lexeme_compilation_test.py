#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[1]
COMPACT_DB = ROOT / "whitakers-words/poc/compact-db"
sys.path.insert(0, str(COMPACT_DB))


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


PREPARE = load_module("compile_test_prepare", COMPACT_DB / "prepare_lexeme_review.py")
VALIDATE = load_module(
    "validate_lexeme_decisions", COMPACT_DB / "validate_lexeme_decisions.py"
)
COMPILE = load_module("compile_lexemes", COMPACT_DB / "compile_lexemes.py")


def candidate(lemma: str = "zazus") -> dict:
    witnesses = [
        {
            "source": "gaffiot",
            "source_family": "gaffiot",
            "independent_authority": True,
            "source_entry_id": "g-1",
            "lemma": lemma,
            "part_of_speech_inferred": False,
            "head": f"{lemma}, gaffiot test gloss",
        },
        {
            "source": "lewis-short-ls-dict",
            "source_family": "lewis",
            "independent_authority": True,
            "source_entry_id": "ls-1",
            "lemma": lemma,
            "part_of_speech_inferred": False,
            "head": f"{lemma}, Lewis and Short test gloss",
        },
    ]
    return PREPARE.prepare_candidate(
        {
            "schema": PREPARE.INPUT_SCHEMA,
            "ascii_lemma": lemma,
            "marked_lemmas": [lemma],
            "lexical": {
                "part_of_speech": "ADJ",
                "paradigm": {
                    "part_of_speech": "ADJ",
                    "declension_or_conjugation": 1,
                    "variant": 1,
                },
                "stems": [
                    {"slot": 1, "stem": lemma[:-2]},
                    {"slot": 2, "stem": lemma[:-2]},
                ],
            },
            "readiness": "cross_validated",
            "unresolved_fields": [
                "sense_identity",
                "canonical_meaning",
                "editorial_metadata",
                "quantity_mask",
            ],
            "source_families": ["gaffiot", "lewis"],
            "source_witnesses": witnesses,
            "validation": {"status": "all_slots_attested", "witnesses": []},
        }
    )


def decision(item: dict, identifier: str = "lexdecision:zazus:test") -> dict:
    lemma = item["key"]["ascii_lemma"]
    stem = lemma[:-2]
    return {
        "schema": "whitakers-words.lexeme-editorial-decision.v1",
        "decision_id": identifier,
        "target": {
            "draft_id": item["draft_id"],
            "revision": item["revision"],
            "sense_ids": ["sense-test"],
        },
        "disposition": "accept_new",
        "canonical": {
            "dictionary_form": f"{lemma}, {stem}a, {stem}um",
            "meaning": "synthetic imported adjective;",
            "part_of_speech": "adjective",
            "properties": {"declension": 1, "variant": 1, "degree": "positive"},
            "stems": [
                {"slot": 1, "ascii": stem},
                {"slot": 2, "ascii": stem},
            ],
            "metadata": {
                "age": "classical",
                "subject": None,
                "geography": None,
                "frequency": "uncommon",
                "source": "other-dictionaries",
            },
        },
        "field_evidence": {
            "dictionary_form": ["lewis-short-ls-dict:ls-1"],
            "meaning": ["lewis-short-ls-dict:ls-1"],
            "properties": ["gaffiot:g-1", "lewis-short-ls-dict:ls-1"],
        },
        "quantity_evidence_ids": [],
        "review": {
            "reviewed_at": "2026-08-29",
            "reviewer": "test-editor",
            "note": "synthetic import fixture",
        },
    }


class LexemeCompilationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validator = jsonschema.Draft202012Validator(
            VALIDATE.load_schema(VALIDATE.DECISION_SCHEMA_PATH),
            format_checker=jsonschema.FormatChecker(),
        )

    def test_compiles_valid_acceptance_to_numeric_microdata(self) -> None:
        item = candidate()
        value = decision(item)
        report = VALIDATE.validate(
            {item["draft_id"]: item},
            [VALIDATE.JsonlRecord(1, value)],
            self.validator,
        )
        self.assertEqual(report["status"], "valid")

        compiled = COMPILE.compile_records([VALIDATE.JsonlRecord(1, value)])

        self.assertEqual(len(compiled), 1)
        record = compiled[0]
        self.assertEqual(record["schema"], COMPILE.OUTPUT_SCHEMA)
        self.assertEqual(record["stems"], ["zaz", "zaz", "", ""])
        self.assertEqual(record["part_of_speech"], 4)
        self.assertEqual(record["paradigm"], 0x11)
        self.assertEqual(record["class_payload"], 1)
        self.assertEqual(record["translation"] & 0x0F, 3)
        self.assertEqual((record["translation"] >> 13) & 0x0F, 5)
        self.assertEqual((record["translation"] >> 17) & 0x1F, 17)

    def test_ledger_order_does_not_change_compiled_order(self) -> None:
        first_item = candidate("zazus")
        second_item = candidate("zezus")
        first = decision(first_item)
        second = decision(second_item, "lexdecision:zezus:test")
        second["target"]["draft_id"] = second_item["draft_id"]
        second["target"]["revision"] = second_item["revision"]
        second["canonical"]["dictionary_form"] = "zezus, zeza, zezum"
        second["canonical"]["stems"] = [
            {"slot": 1, "ascii": "zez"},
            {"slot": 2, "ascii": "zez"},
        ]

        normal = COMPILE.render_jsonl(
            COMPILE.compile_records(
                [VALIDATE.JsonlRecord(1, first), VALIDATE.JsonlRecord(2, second)]
            )
        )
        reversed_output = COMPILE.render_jsonl(
            COMPILE.compile_records(
                [VALIDATE.JsonlRecord(1, second), VALIDATE.JsonlRecord(2, first)]
            )
        )

        self.assertEqual(normal, reversed_output)

    def test_rejects_properties_that_runtime_would_discard(self) -> None:
        item = candidate()
        value = decision(item)
        value["canonical"]["properties"]["editorialOnly"] = "lost"

        with self.assertRaisesRegex(COMPILE.CompileError, "would be lost"):
            COMPILE.compile_records([VALIDATE.JsonlRecord(1, value)])

    def test_rejects_meaning_outside_u8_string_record(self) -> None:
        item = candidate()
        value = decision(item)
        value["canonical"]["meaning"] = "á" * 128

        with self.assertRaisesRegex(COMPILE.CompileError, "255 UTF-8 bytes"):
            COMPILE.compile_records([VALIDATE.JsonlRecord(1, value)])

    def test_allows_semantic_classes_without_class_properties(self) -> None:
        item = candidate()
        value = decision(item)
        value["canonical"]["part_of_speech"] = "conjunction"
        value["canonical"]["properties"] = {}

        self.assertEqual(list(self.validator.iter_errors(value)), [])
        compiled = COMPILE.compile_decision(value)
        self.assertEqual(compiled["part_of_speech"], 11)
        self.assertEqual(compiled["paradigm"], 0)
        self.assertEqual(compiled["class_payload"], 0)


if __name__ == "__main__":
    unittest.main()
