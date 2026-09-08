#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from collections import Counter
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/analyze_semantic_alignment.py"
SPEC = importlib.util.spec_from_file_location("analyze_semantic_alignment", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load semantic alignment analyzer")
ALIGN = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ALIGN
SPEC.loader.exec_module(ALIGN)


def source(
    name: str, family: str, identifier: str, lemma: str, definition: str,
    *, quantity: str | None = None, homograph: int | None = None,
) -> dict:
    meanings = {"definition": definition}
    if name == "ls_dict":
        meanings = {"gloss_pt": definition, "head": definition}
    observations = []
    if quantity is not None:
        observations.append({"position": 1, "letter": "e", "quantity": quantity})
    return {
        "source": name,
        "source_family": family,
        "language": "pt",
        "primary_consensus_authority": family in ALIGN.PRIMARY_FAMILIES,
        "source_entry_id": identifier,
        "homograph_number": homograph,
        "citation": {"source": lemma, "ascii": "levis", "proper": False, "quantity_observations": observations},
        "lexical": {"part_of_speech": "ADJ", "gender": None, "indeclinable": False},
        "morphology": {},
        "meanings": meanings,
        "metadata_and_flags": {},
        "current_words_match_ids": [],
    }


def packet() -> dict:
    value = {
        "schema": "whitakers-words.lexical-comparison-packet.v1",
        "key": {"ascii_lemma": "levis", "proper": False},
        "revision": "sha256:" + "a" * 64,
        "source_entries": [
            source("ls_dict", "lewis", "light", "lĕvis", "leve ligeiro pouco pesado", quantity="short"),
            source("ls_dict", "lewis", "smooth", "lēvis", "liso plano polido", quantity="long"),
            source("faria_v3", "faria", "smooth", "levis", "liso igual polido", homograph=1),
            source("faria_v3", "faria", "light", "levis", "leve ligeiro fraco", homograph=2),
        ],
        "current_words_entries": [],
    }
    return value


class SemanticAlignmentTest(unittest.TestCase):
    def frequencies(self, value: dict) -> tuple[Counter, Counter]:
        frequency = Counter()
        documents = Counter()
        for node in ALIGN.packet_nodes(value):
            for section in node.sections:
                documents[section.language] += 1
                frequency.update((section.language, token) for token in section.tokens)
        return frequency, documents

    def test_aligns_reversed_homograph_numbers_by_meaning(self) -> None:
        value = packet()
        frequency, documents = self.frequencies(value)

        result = ALIGN.analyze_packet(value, frequency, documents, 0.22, 0.10)

        assert result is not None
        pairs = {
            frozenset((item["left"], item["right"])): item
            for item in result["analysis"][0]["pair_candidates"]
        }
        smooth = pairs[frozenset(("ls_dict:smooth", "faria_v3:smooth"))]
        light = pairs[frozenset(("ls_dict:light", "faria_v3:light"))]
        crossed = pairs[frozenset(("ls_dict:smooth", "faria_v3:light"))]
        self.assertEqual(smooth["relation"], "strong_same_lexeme_candidate")
        self.assertEqual(light["relation"], "strong_same_lexeme_candidate")
        self.assertLess(crossed["semantic"]["best_score"], smooth["semantic"]["best_score"])

        components = [
            set(item["members"])
            for item in result["analysis"][0]["proposed_components"]
            if item["members"]
        ]
        self.assertIn({"ls_dict:smooth", "faria_v3:smooth"}, components)
        self.assertIn({"ls_dict:light", "faria_v3:light"}, components)

    def test_equal_semantic_targets_remain_unassigned(self) -> None:
        left = ALIGN.Node(
            "ls_dict:amo", "ls_dict", "lewis", True, "VERB", None, False,
            None, (), (), "love", (),
        )
        first = ALIGN.Node(
            "words:1", "words", "current-words", False, "VERB", None, None,
            None, (1,), (), "love", (),
        )
        second = ALIGN.Node(
            "words:2", "words", "current-words", False, "VERB", None, None,
            None, (2,), (), "love", (),
        )
        pairs = [
            {
                "left": left.ref,
                "right": target.ref,
                "relation": "strong_same_lexeme_candidate",
                "ranking_score": 0.8,
                "semantic": {"best_score": 0.8},
            }
            for target in (first, second)
        ]

        components = ALIGN.propose_components("VERB", [left, first, second], pairs)

        self.assertTrue(all(len(item.get("members", [])) <= 1 for item in components))
        self.assertTrue(all(pair["alignment_edge"]["status"].startswith("rejected") for pair in pairs))

    def test_never_merges_two_entries_from_one_family(self) -> None:
        value = packet()
        frequency, documents = self.frequencies(value)
        result = ALIGN.analyze_packet(value, frequency, documents, 0.01, 0.001)
        assert result is not None

        for component in result["analysis"][0]["proposed_components"]:
            members = component.get("members", [])
            families = [
                ref.split(":", 1)[0] if ref.startswith("words:") else
                {"ls_dict": "lewis", "faria_v3": "faria"}[ref.split(":", 1)[0]]
                for ref in members
            ]
            self.assertEqual(len(families), len(set(families)))

    def test_output_conforms_to_review_schema(self) -> None:
        value = packet()
        frequency, documents = self.frequencies(value)
        result = ALIGN.analyze_packet(value, frequency, documents, 0.22, 0.10)
        assert result is not None
        schema = json.loads(
            (ROOT / "schemas/semantic-alignment-review-v1.schema.json").read_text(encoding="utf-8")
        )
        jsonschema.Draft202012Validator(schema).validate(result)


if __name__ == "__main__":
    unittest.main()
