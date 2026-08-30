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


PREPARE = load_module("prepare_lexeme_review", COMPACT_DB / "prepare_lexeme_review.py")
VALIDATE = load_module(
    "validate_lexeme_decisions", COMPACT_DB / "validate_lexeme_decisions.py"
)


def witness(source: str, family: str, identifier: str, lemma: str) -> dict:
    return {
        "source": source,
        "source_family": family,
        "independent_authority": True,
        "source_entry_id": identifier,
        "lemma": lemma,
        "part_of_speech_inferred": False,
        "head": f"{lemma}, source gloss",
    }


def candidate(*, baseline: dict | None = None) -> dict:
    draft = {
        "schema": PREPARE.INPUT_SCHEMA,
        "ascii_lemma": "novus",
        "marked_lemmas": ["nŏvus"],
        "lexical": {
            "part_of_speech": "ADJ",
            "paradigm": {
                "part_of_speech": "ADJ",
                "declension_or_conjugation": 1,
                "variant": 1,
            },
            "stems": [
                {"slot": 1, "stem": "nov"},
                {"slot": 2, "stem": "nov"},
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
        "source_witnesses": [
            witness("gaffiot", "gaffiot", "g-1", "nŏvus"),
            witness("lewis-short-ls-dict", "lewis", "ls-1", "nŏvus"),
        ],
        "validation": {"status": "all_slots_attested", "witnesses": []},
    }
    return PREPARE.prepare_candidate(draft, baseline)


def decision(record: dict, *, disposition: str = "accept_new") -> dict:
    value = {
        "schema": "whitakers-words.lexeme-editorial-decision.v1",
        "decision_id": "lexdecision:novus:sense-new",
        "target": {
            "draft_id": record["draft_id"],
            "revision": record["revision"],
            "sense_ids": ["sense-new"],
        },
        "disposition": disposition,
        "review": {
            "reviewed_at": "2026-08-29",
            "reviewer": "test-editor",
            "note": "unit-test decision",
        },
    }
    if disposition == "accept_new":
        value.update(
            {
                "canonical": {
                    "dictionary_form": "nŏvus, nova, novum",
                    "meaning": "new; fresh;",
                    "part_of_speech": "adjective",
                    "properties": {
                        "declension": 1,
                        "variant": 1,
                        "degree": "positive",
                    },
                    "stems": [
                        {"slot": 1, "ascii": "nov"},
                        {"slot": 2, "ascii": "nov"},
                    ],
                    "metadata": {
                        "age": None,
                        "subject": None,
                        "geography": None,
                        "frequency": None,
                        "source": "other-dictionaries",
                    },
                },
                "field_evidence": {
                    "dictionary_form": ["lewis-short-ls-dict:ls-1"],
                    "meaning": ["lewis-short-ls-dict:ls-1"],
                    "properties": ["gaffiot:g-1", "lewis-short-ls-dict:ls-1"],
                },
                "quantity_evidence_ids": [],
            }
        )
    return value


class LexemeDecisionValidationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        schema = VALIDATE.load_schema(VALIDATE.DECISION_SCHEMA_PATH)
        cls.validator = jsonschema.Draft202012Validator(
            schema, format_checker=jsonschema.FormatChecker()
        )

    def run_validation(
        self,
        queue: list[dict],
        decisions: list[dict],
        evidence_ids: frozenset[str] | None = None,
    ) -> dict:
        records = tuple(
            VALIDATE.JsonlRecord(index + 1, value)
            for index, value in enumerate(decisions)
        )
        return VALIDATE.validate(
            {item["draft_id"]: item for item in queue},
            records,
            self.validator,
            evidence_ids,
        )

    def test_empty_ledger_is_valid_but_candidate_remains_unresolved(self) -> None:
        item = candidate()
        report = self.run_validation([item], [])

        self.assertEqual(report["status"], "valid")
        self.assertEqual(report["valid_decisions"], 0)
        self.assertEqual(report["unresolved_candidates"], [item["draft_id"]])

    def test_accept_new_requires_exact_reviewed_structure_and_known_sources(self) -> None:
        item = candidate()
        report = self.run_validation([item], [decision(item)])

        self.assertEqual(report["status"], "valid")
        self.assertEqual(report["valid_decisions"], 1)
        self.assertEqual(report["resolved_candidates"], 1)
        self.assertEqual(report["errors"], [])

    def test_stale_revision_is_rejected(self) -> None:
        item = candidate()
        value = decision(item)
        value["target"]["revision"] = "sha256:" + "0" * 64

        report = self.run_validation([item], [value])

        self.assertEqual(report["status"], "invalid")
        self.assertEqual(report["stale_decisions"], 1)
        self.assertEqual(report["errors"][0]["code"], "stale_revision")

    def test_schema_failure_is_reported_without_crashing_semantic_checks(self) -> None:
        item = candidate()
        value = decision(item)
        del value["canonical"]

        report = self.run_validation([item], [value])

        self.assertEqual(report["status"], "invalid")
        self.assertTrue(
            any(error["code"] == "schema_violation" for error in report["errors"])
        )

    def test_structure_property_and_provenance_mismatches_are_rejected(self) -> None:
        item = candidate()
        value = decision(item)
        value["canonical"]["stems"][1]["ascii"] = "nover"
        value["canonical"]["properties"]["declension"] = 2
        value["field_evidence"]["meaning"] = ["unknown-source:missing"]

        report = self.run_validation([item], [value])
        codes = {error["code"] for error in report["errors"]}

        self.assertIn("canonical_stems_mismatch", codes)
        self.assertIn("canonical_property_mismatch", codes)
        self.assertIn("unknown_field_evidence", codes)

    def test_overlapping_sense_decisions_invalidate_both_lines(self) -> None:
        item = candidate()
        first = decision(item, disposition="defer")
        second = decision(item, disposition="reject")
        second["decision_id"] = "lexdecision:novus:sense-new-reject"

        report = self.run_validation([item], [first, second])

        overlaps = [
            error
            for error in report["errors"]
            if error["code"] == "overlapping_sense_decision"
        ]
        self.assertEqual(len(overlaps), 2)
        self.assertEqual(report["invalid_decisions"], 2)

    def test_duplicate_decision_ids_invalidate_every_occurrence(self) -> None:
        item = candidate()
        first = decision(item, disposition="defer")
        second = decision(item, disposition="reject")
        second["target"]["sense_ids"] = ["sense-another"]

        report = self.run_validation([item], [first, second])
        duplicates = [
            error
            for error in report["errors"]
            if error["code"] == "duplicate_decision_id"
        ]

        self.assertEqual(len(duplicates), 2)
        self.assertEqual(report["invalid_decisions"], 2)

    def test_accept_new_structural_collision_invalidates_both_decisions(self) -> None:
        first_candidate = candidate()
        second_candidate = json.loads(json.dumps(first_candidate))
        second_candidate["draft_id"] = "lexdraft:adj:common:novum"
        second_candidate["revision"] = "sha256:" + "b" * 64
        second_candidate["key"]["ascii_lemma"] = "novum"

        first = decision(first_candidate)
        second = decision(second_candidate)
        second["decision_id"] = "lexdecision:novum:sense-new"
        second["target"]["sense_ids"] = ["sense-new-neuter"]
        second["canonical"]["dictionary_form"] = "nŏvum, nova, novum"

        report = self.run_validation(
            [first_candidate, second_candidate], [first, second]
        )
        collisions = [
            error
            for error in report["errors"]
            if error["code"] == "accepted_structure_collision"
        ]

        self.assertEqual(len(collisions), 2)
        self.assertEqual(report["invalid_decisions"], 2)

    def test_quantity_references_require_and_resolve_against_manifest(self) -> None:
        item = candidate()
        value = decision(item)
        value["quantity_evidence_ids"] = ["quantity-novus"]

        absent = self.run_validation([item], [value])
        missing = self.run_validation([item], [value], frozenset())
        present = self.run_validation(
            [item], [value], frozenset({"quantity-novus"})
        )

        self.assertEqual(absent["errors"][0]["code"], "quantity_manifest_required")
        self.assertEqual(missing["errors"][0]["code"], "unknown_quantity_evidence")
        self.assertEqual(present["status"], "valid")

    def test_merge_target_must_exist_in_pinned_baseline(self) -> None:
        baseline = {
            "schema": "whitakers-words.analysis",
            "schemaVersion": 1,
            "query": {"text": "novus", "normalized": "novus", "mode": "latin"},
            "status": "analyzed",
            "analyses": [
                {
                    "partOfSpeech": "adjective",
                    "lexeme": {
                        "dictionary": "general",
                        "entryId": 42,
                        "dictionaryForm": "novus, nova, novum",
                        "partOfSpeech": "adjective",
                        "meaning": "new;",
                    },
                    "derivation": {"method": "regular", "steps": []},
                }
            ],
            "diagnostics": [],
        }
        item = candidate(baseline=baseline)
        valid = decision(item, disposition="merge_existing")
        valid["existing_lexeme"] = {"dictionary": "general", "entry_id": 42}
        invalid = json.loads(json.dumps(valid))
        invalid["existing_lexeme"]["entry_id"] = 43

        self.assertEqual(self.run_validation([item], [valid])["status"], "valid")
        report = self.run_validation([item], [invalid])
        self.assertEqual(report["errors"][0]["code"], "merge_target_not_in_baseline")


if __name__ == "__main__":
    unittest.main()
