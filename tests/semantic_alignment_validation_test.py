#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/validate_semantic_alignment.py"
SPEC = importlib.util.spec_from_file_location("validate_semantic_alignment", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load semantic alignment validator")
VALIDATE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATE
SPEC.loader.exec_module(VALIDATE)


REVISION = "sha256:" + "a" * 64


def review() -> dict:
    return {
        "schema": "whitakers-words.semantic-alignment-review.v1",
        "packet_key": {"ascii_lemma": "levis", "proper": False},
        "revision": REVISION,
        "analysis": [
            {
                "part_of_speech": "ADJ",
                "nodes": [
                    {"ref": "ls_dict:light", "source_family": "lewis", "primary_consensus_authority": True, "gender": None, "indeclinable": False, "quantity_observations": [{"position": 1, "quantity": "short"}]},
                    {"ref": "faria_v3:light", "source_family": "faria", "primary_consensus_authority": True, "gender": None, "indeclinable": False, "quantity_observations": [{"position": 1, "quantity": "short"}]},
                    {"ref": "words:1", "source_family": "current-words", "primary_consensus_authority": False, "gender": None, "indeclinable": None, "quantity_observations": [{"position": 1, "quantity": "short"}]},
                    {"ref": "ls_dict:smooth", "source_family": "lewis", "primary_consensus_authority": True, "gender": None, "indeclinable": False, "quantity_observations": [{"position": 1, "quantity": "long"}]},
                    {"ref": "faria_v3:smooth", "source_family": "faria", "primary_consensus_authority": True, "gender": None, "indeclinable": False, "quantity_observations": [{"position": 1, "quantity": "long"}]},
                ],
            }
        ],
    }


def decision(identifier: str, members: list[str], summary: str = "leve") -> VALIDATE.Record:
    return VALIDATE.Record(
        1,
        {
            "schema": "whitakers-words.semantic-alignment-decision.v1",
            "decision_id": identifier,
            "target": {
                "packet_key": {"ascii_lemma": "levis", "proper": False},
                "analysis_revision": REVISION,
                "part_of_speech": "ADJ",
                "members": members,
            },
            "disposition": "accept_alignment",
            "sense_summary": summary,
            "review": {"reviewed_at": "2026-09-03", "reviewer": "test", "note": "checked meanings"},
        },
    )


class SemanticAlignmentValidationTest(unittest.TestCase):
    def test_resolves_two_homographs_and_computes_consensus(self) -> None:
        value = review()
        reviews = {("levis", False): value}
        decisions = (
            decision("semdecision:levis:light", ["ls_dict:light", "faria_v3:light", "words:1"]),
            decision("semdecision:levis:smooth", ["ls_dict:smooth", "faria_v3:smooth"], "liso"),
        )

        report, resolved = VALIDATE.validate(reviews, decisions)

        self.assertTrue(report["valid"])
        self.assertEqual(len(resolved), 2)
        self.assertEqual(resolved[0]["lexical_action_hint"], "merge_existing")
        self.assertEqual(resolved[0]["consensus"]["vowel_quantity"][0]["status"], "majority_2_of_3")
        self.assertEqual(resolved[1]["lexical_action_hint"], "new_lexeme_candidate")

    def test_rejects_stale_revision_and_family_collision(self) -> None:
        value = review()
        stale = decision("semdecision:levis:stale", ["ls_dict:light", "faria_v3:light"])
        stale.value["target"]["analysis_revision"] = "sha256:" + "b" * 64
        collision = decision("semdecision:levis:collision", ["ls_dict:light", "ls_dict:smooth"])
        collision = VALIDATE.Record(2, collision.value)

        report, resolved = VALIDATE.validate({("levis", False): value}, (stale, collision))

        self.assertFalse(report["valid"])
        self.assertEqual(resolved, ())
        self.assertEqual({item["code"] for item in report["errors"]}, {"stale_revision", "source_family_collision"})

    def test_rejects_overlapping_accepted_groups(self) -> None:
        value = review()
        first = decision("semdecision:levis:first", ["ls_dict:light", "faria_v3:light"])
        second = decision("semdecision:levis:second", ["ls_dict:light", "words:1"])
        second = VALIDATE.Record(2, second.value)

        report, resolved = VALIDATE.validate({("levis", False): value}, (first, second))

        self.assertFalse(report["valid"])
        self.assertEqual(resolved, ())
        self.assertEqual(sum(item["code"] == "overlapping_accepted_alignment" for item in report["errors"]), 2)


if __name__ == "__main__":
    unittest.main()
