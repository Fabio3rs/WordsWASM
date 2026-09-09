#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/build_u16_appendix_poc.py"
SPEC = importlib.util.spec_from_file_location("build_u16_appendix_poc", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load u16 appendix PoC builder")
POC = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = POC
SPEC.loader.exec_module(POC)


def candidate(lemma: str, part: str = "NOUN") -> dict:
    return {
        "ascii_lemma": lemma,
        "part_of_speech": part,
        "proper": False,
        "support": "corroborated_independent",
        "independent_family_count": 2,
        "independent_families": ["faria", "lewis"],
        "witness_count": 2,
        "genders": ["n"],
        "witnesses": [
            {
                "source": "faria-v3-quality",
                "source_family": "faria",
                "source_entry_id": f"faria:{lemma}",
                "head": f"{lemma}; significado sintético",
            },
            {
                "source": "lewis-short-ls-dict",
                "source_family": "lewis",
                "source_entry_id": f"ls:{lemma}",
                "head": f"{lemma}, synthetic meaning",
            },
        ],
        "proposed_words_paradigm": None,
        "proposed_words_stems": None,
        "latin_german_form_validation": {"status": "unavailable"},
    }


class U16AppendixPocTest(unittest.TestCase):
    def test_max_cardinality_fills_exact_reference_capacity(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            dictionary = root / "DICTFILE.GEN"
            stems = root / "STEMFILE.GEN"
            uniques = root / "UNIQUES.LAT"
            dictionary.write_bytes(b" " * POC.DICTIONARY_RECORD_SIZE)
            stems.write_bytes(b" " * (POC.STEM_RECORD_SIZE * (POC.U16_MAX - 3)))
            uniques.write_text("", encoding="utf-8")
            candidates = root / "candidates.jsonl"
            candidates.write_text(
                POC.render_jsonl(
                    [candidate("alpha"), candidate("beta", "ADV"), candidate("gamma")]
                ),
                encoding="utf-8",
            )
            audit = root / "audit.json"
            audit.write_text(
                json.dumps(
                    {
                        "morphology_crosswalk": {"mappings": []},
                        "stem_template_crosswalk": {"mappings": []},
                    }
                ),
                encoding="utf-8",
            )
            arguments = Namespace(
                candidates=candidates,
                audit_report=audit,
                dictionary=dictionary,
                stems=stems,
                uniques=uniques,
                profile="max-cardinality",
                output=root / "LEXEMES.LAT",
                selection_output=root / "selection.jsonl",
                report=root / "report.json",
            )

            report = POC.build(arguments)

            self.assertEqual(report["selection"]["lexemes"], 3)
            self.assertEqual(report["result"]["stem_references"], POC.U16_MAX)
            self.assertEqual(report["result"]["stem_reference_counter_remaining"], 0)
            records = list(POC.read_jsonl(arguments.output))
            self.assertEqual(records[0]["stems"], ["beta", "", "", ""])
            self.assertEqual(records[1]["paradigm"], 0x99)

    def test_relaxed_proposal_uses_majority_paradigm_and_template(self) -> None:
        item = candidate("novus", "ADJ")
        item["witnesses"][0].update(
            {
                "morphology_key": "test:model",
                "external_stems": [{"number": 1, "stem": "nov"}],
            }
        )
        paradigms = {
            "test:model": {
                "part_of_speech": "ADJ",
                "declension_or_conjugation": 1,
                "variant": 1,
                "witnesses": 20,
            }
        }
        templates = {
            "test:model": {
                "witnesses": 18,
                "slots": [
                    {"words_slot": 1, "external_radical": 1},
                    {"words_slot": 2, "external_radical": 1},
                ],
            }
        }

        proposal = POC.relaxed_proposal(
            "morphology-first", item, paradigms, templates
        )

        self.assertIsNotNone(proposal)
        record, method = proposal
        self.assertEqual(method, "majority-map")
        self.assertEqual(record["stems"], ["nov", "nov", "", ""])
        self.assertEqual(record["paradigm"], 0x11)


if __name__ == "__main__":
    unittest.main()
