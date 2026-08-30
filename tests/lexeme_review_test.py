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
COMPACT_DB = ROOT / "whitakers-words/poc/compact-db"
sys.path.insert(0, str(COMPACT_DB))
SCRIPT = COMPACT_DB / "prepare_lexeme_review.py"
SPEC = importlib.util.spec_from_file_location("prepare_lexeme_review", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load lexeme review generator")
REVIEW = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = REVIEW
SPEC.loader.exec_module(REVIEW)


def witness(
    source: str,
    family: str,
    identifier: str,
    lemma: str,
    *,
    independent: bool = True,
    inferred: bool = False,
    head: str | None = None,
) -> dict[str, object]:
    return {
        "source": source,
        "source_family": family,
        "independent_authority": independent,
        "source_entry_id": identifier,
        "lemma": lemma,
        "part_of_speech_inferred": inferred,
        "head": head or f"{lemma}, test gloss",
    }


def draft(
    lemma: str,
    witnesses: list[dict[str, object]],
    *,
    readiness: str = "cross_validated",
) -> dict[str, object]:
    return {
        "schema": REVIEW.INPUT_SCHEMA,
        "ascii_lemma": lemma,
        "marked_lemmas": sorted({str(item["lemma"]) for item in witnesses}),
        "lexical": {
            "part_of_speech": "ADJ",
            "paradigm": {
                "part_of_speech": "ADJ",
                "declension_or_conjugation": 1,
                "variant": 1,
            },
            "stems": [{"slot": 1, "stem": lemma[:-2]}],
        },
        "readiness": readiness,
        "unresolved_fields": [
            "sense_identity",
            "canonical_meaning",
            "editorial_metadata",
            "quantity_mask",
        ],
        "source_families": sorted(
            {
                str(item["source_family"])
                for item in witnesses
                if item["independent_authority"]
            }
        ),
        "source_witnesses": witnesses,
        "validation": {"status": "all_slots_attested", "witnesses": []},
    }


class LexemeReviewTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.review_schema = json.loads(
            (ROOT / "schemas/lexeme-review-candidate-v1.schema.json").read_text(
                encoding="utf-8"
            )
        )
        cls.decision_schema = json.loads(
            (ROOT / "schemas/lexeme-editorial-decision-v1.schema.json").read_text(
                encoding="utf-8"
            )
        )
        jsonschema.Draft202012Validator.check_schema(cls.review_schema)
        jsonschema.Draft202012Validator.check_schema(cls.decision_schema)

    def test_two_independent_families_form_consensus_but_unmarked_is_unknown(self) -> None:
        record = REVIEW.prepare_candidate(
            draft(
                "malum",
                [
                    witness("ls", "lewis", "ls-1", "mālum"),
                    witness("gaffiot", "gaffiot", "g-1", "mālum"),
                    witness("german", "latin-german", "de-1", "malum"),
                ],
            )
        )

        quantity = record["evidence"]["quantity"]
        by_index = {item["letter_index"]: item for item in quantity["positions"]}
        self.assertEqual(quantity["status"], "consensus_partial")
        self.assertEqual(by_index[1]["consensus"], "long")
        self.assertEqual(by_index[3]["status"], "unknown")
        self.assertNotIn("consensus", by_index[3])
        self.assertFalse(record["decision"]["automatic_promotion_allowed"])

    def test_independent_quantity_conflict_never_uses_majority(self) -> None:
        record = REVIEW.prepare_candidate(
            draft(
                "malum",
                [
                    witness("ls", "lewis", "ls-1", "mālum"),
                    witness("gaffiot", "gaffiot", "g-1", "mălum"),
                    witness("faria", "faria", "f-1", "mālum"),
                ],
            )
        )

        position = record["evidence"]["quantity"]["positions"][0]
        self.assertEqual(position["status"], "conflict")
        self.assertNotIn("consensus", position)
        self.assertIn(
            "independent_quantity_conflict", record["triage"]["flags"]
        )
        self.assertEqual(record["triage"]["priority"], "high")

    def test_one_independent_vote_does_not_override_derived_disagreement(self) -> None:
        record = REVIEW.prepare_candidate(
            draft(
                "malum",
                [
                    witness("ls", "lewis", "ls-1", "mālum"),
                    witness(
                        "collatinus",
                        "collatinus-derived",
                        "c-1",
                        "mălum",
                        independent=False,
                    ),
                    witness("german", "latin-german", "de-1", "malum"),
                ],
            )
        )

        position = record["evidence"]["quantity"]["positions"][0]
        self.assertEqual(position["status"], "conflict")
        self.assertIn("derived_quantity_disagreement", record["triage"]["flags"])
        self.assertNotIn(
            "independent_quantity_conflict", record["triage"]["flags"]
        )

    def test_revision_pins_all_generated_evidence(self) -> None:
        original = draft(
            "malum",
            [
                witness("ls", "lewis", "ls-1", "mālum"),
                witness("gaffiot", "gaffiot", "g-1", "mālum"),
            ],
        )
        first = REVIEW.prepare_candidate(original)
        again = REVIEW.prepare_candidate(json.loads(json.dumps(original)))
        changed = json.loads(json.dumps(original))
        changed["source_witnesses"][0]["head"] = "mālum, changed gloss"
        second = REVIEW.prepare_candidate(changed)

        self.assertEqual(first["revision"], again["revision"])
        self.assertNotEqual(first["revision"], second["revision"])
        self.assertEqual(first["draft_id"], second["draft_id"])

    def test_duplicate_draft_identity_is_rejected(self) -> None:
        value = draft(
            "malum",
            [
                witness("ls", "lewis", "ls-1", "mālum"),
                witness("gaffiot", "gaffiot", "g-1", "mālum"),
            ],
        )
        with self.assertRaisesRegex(REVIEW.ReviewError, "duplicate lexdraft"):
            REVIEW.prepare([value, value])

    def test_baseline_distinguishes_direct_analysis_with_another_pos(self) -> None:
        value = draft(
            "fidele",
            [
                witness("ls", "lewis", "ls-1", "fĭdēlē"),
                witness("gaffiot", "gaffiot", "g-1", "fĭdēlĕ"),
            ],
        )
        value["lexical"]["part_of_speech"] = "ADV"
        value["lexical"]["paradigm"]["part_of_speech"] = "ADV"
        baseline = {
            "schema": "whitakers-words.analysis",
            "schemaVersion": 1,
            "query": {"text": "fidele", "normalized": "fidele", "mode": "latin"},
            "status": "analyzed",
            "analyses": [
                {
                    "partOfSpeech": "adjective",
                    "lexeme": {
                        "dictionary": "general",
                        "entryId": 1,
                        "dictionaryForm": "fidelis, fidele",
                        "partOfSpeech": "adjective",
                        "meaning": "faithful;",
                    },
                    "derivation": {"method": "regular", "steps": []},
                }
            ],
            "diagnostics": [],
        }

        record = REVIEW.prepare([value], {"fidele": baseline})[0]

        self.assertEqual(record["baseline"]["coverage"], "direct")
        self.assertFalse(record["baseline"]["target_part_of_speech_present"])
        self.assertIn("baseline_direct", record["triage"]["flags"])
        self.assertIn(
            "baseline_different_part_of_speech", record["triage"]["flags"]
        )
        self.assertRegex(record["baseline_revision"], r"^sha256:[0-9a-f]{64}$")
        jsonschema.validate(record, self.review_schema)

    def test_cli_products_are_deterministic_and_report_zero_promotions(self) -> None:
        values = [
            draft(
                "novus",
                [
                    witness("ls", "lewis", "ls-1", "nŏvus"),
                    witness("gaffiot", "gaffiot", "g-1", "nŏvus"),
                ],
            ),
            draft(
                "malum",
                [
                    witness("ls", "lewis", "ls-2", "mālum"),
                    witness("gaffiot", "gaffiot", "g-2", "mālum"),
                ],
            ),
        ]
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "drafts.jsonl"
            path.write_text(REVIEW.render_jsonl(values), encoding="utf-8")
            records = REVIEW.prepare(REVIEW.read_jsonl(path))

        self.assertEqual(
            [item["key"]["ascii_lemma"] for item in records],
            ["malum", "novus"],
        )
        summary = REVIEW.report(records)
        self.assertEqual(summary["records"], 2)
        self.assertEqual(summary["automatic_promotions"], 0)
        self.assertEqual(summary["decision_status"], {"needs_review": 2})

        validator = jsonschema.Draft202012Validator(self.review_schema)
        for record in records:
            validator.validate(record)

    def test_decision_schema_requires_pinned_revision_and_canonical_acceptance(self) -> None:
        base = {
            "schema": "whitakers-words.lexeme-editorial-decision.v1",
            "decision_id": "lexdecision:malum:sense-apple",
            "target": {
                "draft_id": "lexdraft:noun:common:malum",
                "revision": "sha256:" + "a" * 64,
                "sense_ids": ["sense-apple"],
            },
            "disposition": "defer",
            "review": {
                "reviewed_at": "2026-08-29",
                "reviewer": "test-editor",
                "note": "awaiting another source",
            },
        }
        validator = jsonschema.Draft202012Validator(self.decision_schema)
        validator.validate(base)

        invalid_accept = json.loads(json.dumps(base))
        invalid_accept["disposition"] = "accept_new"
        with self.assertRaises(jsonschema.ValidationError):
            validator.validate(invalid_accept)

        invalid_defer = json.loads(json.dumps(base))
        invalid_defer["canonical"] = {}
        with self.assertRaises(jsonschema.ValidationError):
            validator.validate(invalid_defer)


if __name__ == "__main__":
    unittest.main()
