#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

import jsonschema
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
COMPACT = ROOT / "whitakers-words/poc/compact-db"
sys.path.insert(0, str(COMPACT))


def load(name: str):
    path = COMPACT / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


LEXICAL = load("lexical_embedding_db")
GENERATE = load("generate_lexical_embeddings")
RANK = load("rank_lexical_embeddings")
CALIBRATE = load("calibrate_lexical_embeddings")
ALIGN = load("analyze_semantic_alignment")


def external(
    source: str, family: str, identifier: str, lemma: str, definition: str,
    language: str, quantity: str, homograph: int,
) -> dict:
    return {
        "source": source,
        "source_family": family,
        "language": language,
        "primary_consensus_authority": True,
        "source_entry_id": identifier,
        "homograph_number": homograph,
        "citation": {
            "source": lemma,
            "ascii": "levis",
            "proper": False,
            "quantity_observations": [
                {"position": 1, "letter": "e", "quantity": quantity}
            ],
        },
        "lexical": {
            "part_of_speech_raw": "adj",
            "part_of_speech": "ADJ",
            "gender_raw": None,
            "gender": None,
            "indeclinable": False,
        },
        "morphology": {
            "forms": [
                {"form": lemma, "form_norm": "levis", "kind": "orth"},
                {"form": "e", "form_norm": "e", "kind": "itype"},
            ]
        },
        "meanings": {
            "senses": [
                {
                    "sense_id": f"{identifier}:1",
                    "parent_id": None,
                    "n_label": "I",
                    "gloss": definition,
                }
            ]
        },
        "metadata_and_flags": {"provenance": {"fixture": True}},
        "current_words_match_ids": [],
    }


def packet() -> dict:
    return {
        "schema": LEXICAL.INPUT_SCHEMA,
        "key": {"ascii_lemma": "levis", "proper": False},
        "revision": "sha256:" + "a" * 64,
        "source_entries": [
            external("ls_dict", "lewis", "light", "lĕvis", "light and not heavy", "en", "short", 1),
            external("ls_dict", "lewis", "smooth", "lēvis", "smooth and polished", "en", "long", 2),
            external("faria_v3", "faria", "light", "lĕvis", "leve e ligeiro", "pt", "short", 2),
            external("faria_v3", "faria", "smooth", "lēvis", "liso e polido", "pt", "long", 1),
        ],
        "current_words_entries": [
            {
                "entry_id": 7,
                "stems": [
                    {
                        "slot": 1,
                        "ascii": "lev",
                        "quantity_observations": [{"position": 1, "letter": "e", "quantity": "short"}],
                    }
                ],
                "part_of_speech": "ADJ",
                "paradigm": {"declension_or_conjugation": 3, "variant": 1},
                "class_attribute": {"code": 1, "name": "positive"},
                "meaning": "light and not heavy",
                "metadata_and_flags": {},
            }
        ],
    }


class FakeOllama:
    def model_digest(self, model: str) -> str:
        return "fixture-model-digest"

    def embed(self, texts: list[str], model: str) -> list[list[float]]:
        result = []
        for text in texts:
            lowered = text.lower()
            if "smooth" in lowered or "polido" in lowered or "liso" in lowered:
                result.append([0.0, 1.0, 0.0])
            elif "light" in lowered or "leve" in lowered or "ligeiro" in lowered:
                result.append([1.0, 0.0, 0.0])
            else:
                result.append([0.0, 0.0, 1.0])
        return result


class LexicalEmbeddingPipelineTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.dump = self.directory / "comparison.jsonl"
        self.dump.write_text(json.dumps(packet(), ensure_ascii=False) + "\n", encoding="utf-8")
        self.evidence = self.directory / "evidence.sqlite"
        self.cache = self.directory / "cache.sqlite"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def build(self) -> dict:
        return LEXICAL.build_database(self.dump, self.evidence)

    def generate(self) -> dict:
        arguments = argparse.Namespace(
            evidence=self.evidence,
            cache=self.cache,
            model="qwen3-embedding:8b",
            ollama_url="http://unused",
            batch_size=2,
            expected_dimension=3,
            instruction=LEXICAL.DEFAULT_INSTRUCTION,
            timeout=1,
        )
        return GENERATE.generate(arguments, FakeOllama())

    def rank(self, run_id: str) -> dict:
        return RANK.rank(
            argparse.Namespace(
                evidence=self.evidence,
                cache=self.cache,
                run_id=run_id,
            )
        )

    def test_builds_provenance_database_with_senses_and_typed_forms(self) -> None:
        report = self.build()
        self.assertEqual(report["counts"]["entries"], 5)
        self.assertEqual(report["counts"]["documents"], 5)
        with LEXICAL.connect_evidence(self.evidence, readonly=True) as connection:
            row = connection.execute(
                "SELECT lemma_display,homograph_number,content_sha256,semantic_status FROM entry WHERE entry_ref='ls_dict:light'"
            ).fetchone()
            self.assertEqual((row["lemma_display"], row["homograph_number"]), ("lĕvis", 1))
            self.assertRegex(row["content_sha256"], r"^sha256:[0-9a-f]{64}$")
            self.assertEqual(row["semantic_status"], "available")
            roles = {
                value[0]
                for value in connection.execute(
                    "SELECT role FROM entry_form WHERE entry_ref='ls_dict:light'"
                )
            }
            self.assertEqual(roles, {"lemma", "variant", "principal_part"})
            semantic = connection.execute(
                "SELECT semantic_text FROM semantic_document WHERE entry_ref='ls_dict:light'"
            ).fetchone()[0]
            self.assertEqual(semantic, "light and not heavy")

    def test_one_words_entry_can_belong_to_multiple_lemma_packets(self) -> None:
        first = packet()
        second = {
            "schema": LEXICAL.INPUT_SCHEMA,
            "key": {"ascii_lemma": "levo", "proper": False},
            "revision": "sha256:" + "d" * 64,
            "source_entries": [],
            "current_words_entries": [first["current_words_entries"][0]],
        }
        self.dump.write_text(
            json.dumps(first, ensure_ascii=False) + "\n" + json.dumps(second, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

        report = self.build()

        self.assertEqual(report["counts"]["entries"], 5)
        self.assertEqual(report["counts"]["entry_bucket_links"], 6)
        self.assertEqual(report["counts"]["repeated_entry_links"], 1)
        with LEXICAL.connect_evidence(self.evidence, readonly=True) as connection:
            self.assertEqual(
                connection.execute(
                    "SELECT count(*) FROM entry WHERE entry_ref='words:7'"
                ).fetchone()[0],
                1,
            )
            buckets = list(
                connection.execute(
                    "SELECT ascii_lemma,packet_revision FROM entry_bucket "
                    "WHERE entry_ref='words:7' ORDER BY ascii_lemma"
                )
            )
        self.assertEqual([row[0] for row in buckets], ["levis", "levo"])

    def test_embedding_cache_is_normalized_and_resumable(self) -> None:
        self.build()
        first = self.generate()
        second = self.generate()
        self.assertEqual(first["documents"], 5)
        self.assertEqual(first["new_documents"], 5)
        self.assertEqual(second["new_documents"], 0)
        with LEXICAL.connect_evidence(self.cache, readonly=True) as connection:
            blobs = list(connection.execute("SELECT dimension,vector FROM embedding"))
        self.assertEqual(len(blobs), 5)
        for dimension, blob in blobs:
            vector = np.frombuffer(blob, dtype="<f4")
            self.assertEqual(dimension, 3)
            self.assertAlmostEqual(float(np.linalg.norm(vector)), 1.0, places=6)

    def test_ranks_correct_homographs_and_blocks_quantity_conflict(self) -> None:
        self.build()
        generated = self.generate()
        report = self.rank(generated["run_id"])
        self.assertGreater(report["counts"]["candidate_pairs"], 0)
        with LEXICAL.connect_evidence(self.evidence, readonly=True) as connection:
            correct = connection.execute(
                "SELECT max_cosine,mutual_top1,blocking_reason FROM candidate_score "
                "WHERE run_id=? AND left_entry_ref='faria_v3:light' AND right_entry_ref='ls_dict:light'",
                (generated["run_id"],),
            ).fetchone()
            crossed = connection.execute(
                "SELECT max_cosine,blocking_reason FROM candidate_score "
                "WHERE run_id=? AND left_entry_ref='faria_v3:light' AND right_entry_ref='ls_dict:smooth'",
                (generated["run_id"],),
            ).fetchone()
        self.assertAlmostEqual(correct["max_cosine"], 1.0)
        self.assertTrue(correct["mutual_top1"])
        self.assertIsNone(correct["blocking_reason"])
        self.assertLess(crossed["max_cosine"], correct["max_cosine"])
        self.assertEqual(crossed["blocking_reason"], "quantity_conflict")

    def test_v2_embedding_evidence_is_informative_without_calibration(self) -> None:
        self.build()
        generated = self.generate()
        self.rank(generated["run_id"])
        evidence = ALIGN.EmbeddingEvidence(self.evidence, generated["run_id"], None)
        value = packet()
        frequency, documents = ALIGN.collect_document_frequency(self.dump)
        result = ALIGN.analyze_packet(value, frequency, documents, 0.99, 0.98, evidence)
        assert result is not None
        self.assertEqual(result["schema"], ALIGN.OUTPUT_SCHEMA_V2)
        pairs = {
            frozenset((item["left"], item["right"])): item
            for item in result["analysis"][0]["pair_candidates"]
        }
        correct = pairs[frozenset(("ls_dict:light", "faria_v3:light"))]
        self.assertEqual(correct["embedding_evidence"]["max_cosine"], 1.0)
        self.assertFalse(correct["embedding_evidence"]["calibrated_support"])
        self.assertNotEqual(correct["relation"], "embedding_same_lexeme_candidate")
        schema = json.loads(
            (ROOT / "schemas/semantic-alignment-review-v2.schema.json").read_text(encoding="utf-8")
        )
        jsonschema.Draft202012Validator(schema).validate(result)

    def test_passing_calibration_can_propose_cross_language_edge(self) -> None:
        self.build()
        generated = self.generate()
        self.rank(generated["run_id"])
        calibration_path = self.directory / "calibration.json"
        calibration_path.write_text(
            json.dumps(
                {
                    "schema": "whitakers-words.lexical-embedding-calibration.v1",
                    "run_id": generated["run_id"],
                    "minimum_precision": 0.995,
                    "cosine_threshold": 0.9,
                    "minimum_margin": 0.1,
                    "promotion_allowed": True,
                    "gold_sha256": "sha256:" + "b" * 64,
                    "policy": "mutual_top1_and_no_structural_blocker",
                }
            ),
            encoding="utf-8",
        )
        evidence = ALIGN.EmbeddingEvidence(
            self.evidence, generated["run_id"], calibration_path
        )
        frequency, documents = ALIGN.collect_document_frequency(self.dump)
        result = ALIGN.analyze_packet(packet(), frequency, documents, 0.99, 0.98, evidence)
        assert result is not None
        pairs = {
            frozenset((item["left"], item["right"])): item
            for item in result["analysis"][0]["pair_candidates"]
        }
        correct = pairs[frozenset(("ls_dict:light", "faria_v3:light"))]
        crossed = pairs[frozenset(("ls_dict:smooth", "faria_v3:light"))]
        self.assertEqual(correct["relation"], "embedding_same_lexeme_candidate")
        self.assertTrue(correct["embedding_evidence"]["calibrated_support"])
        self.assertFalse(crossed["embedding_evidence"]["calibrated_support"])

    def test_calibration_uses_unedited_candidate_revision_and_holdout_gate(self) -> None:
        records = []
        for index in range(80):
            positive = index % 2 == 0
            record = {
                "schema": CALIBRATE.GOLD_SCHEMA,
                "candidate_revision": "",
                "run_id": "embedding:" + "c" * 64,
                "packet_key": {"ascii_lemma": f"lemma{chr(97 + index % 26)}", "proper": False},
                "left_entry_ref": f"left:{index}",
                "right_entry_ref": f"right:{index}",
                "stratum": "without_words",
                "embedding": {
                    "max_cosine": 0.9 if positive else 0.2,
                    "top3_mean_cosine": 0.85 if positive else 0.15,
                    "left_rank": 1,
                    "right_rank": 1,
                    "mutual_top1": True,
                    "minimum_margin": 0.1,
                    "blocking_reason": None,
                },
                "existing_relation": None,
                "existing_edge_status": None,
                "label": "same_lexeme" if positive else "different_lexeme",
                "reviewer": "fixture",
                "note": "reviewed fixture",
            }
            record["candidate_revision"] = CALIBRATE.candidate_revision(record)
            records.append(record)
        gold = self.directory / "gold.jsonl"
        gold.write_text(
            "".join(json.dumps(record) + "\n" for record in records),
            encoding="utf-8",
        )
        report, configuration = CALIBRATE.evaluate(
            argparse.Namespace(gold=gold, minimum_precision=0.995, expected_records=0)
        )
        self.assertTrue(configuration["promotion_allowed"])
        self.assertGreater(report["development"]["recall"], 0.0)
        self.assertEqual(report["holdout"]["false_positive"], 0)

    def test_long_text_chunking_is_deterministic_and_loss_bounded(self) -> None:
        text = "Prima sententia. " * 2000
        first = LEXICAL.chunk_text(text, maximum=1000, overlap=100)
        second = LEXICAL.chunk_text(text, maximum=1000, overlap=100)
        self.assertEqual(first, second)
        self.assertGreater(len(first), 1)
        self.assertTrue(all(len(piece) <= 1000 for _, _, piece in first))
        self.assertEqual(first[0][0], 0)
        self.assertEqual(first[-1][1], len(text.rstrip()))


if __name__ == "__main__":
    unittest.main()
