#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/grammar_coverage.py"
SPEC = importlib.util.spec_from_file_location("grammar_coverage", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load grammar coverage compiler")
COVERAGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = COVERAGE
SPEC.loader.exec_module(COVERAGE)


class GrammarCoverageTest(unittest.TestCase):
    def test_builds_stable_rule_coverage_and_witnesses(self) -> None:
        documents = [
            {
                "query": {"text": "alpha"},
                "hits": [{"ruleId": 2}, {"ruleId": None}, {"ruleId": 0}],
            },
            {
                "query": {"text": "beta"},
                "hits": [{"ruleId": 2}, {"ruleId": 4}],
            },
        ]
        report = json.loads(COVERAGE.compile_report(documents, 5, "fixture"))
        self.assertEqual(report["observedRuleIds"], [0, 2, 4])
        self.assertEqual(report["missingRuleIds"], [1, 3])
        self.assertEqual(report["witnesses"], {
            "0": ["alpha"],
            "2": ["alpha", "beta"],
            "4": ["beta"],
        })
        self.assertEqual(report["counts"], {
            "rules": 5,
            "observed": 3,
            "missing": 2,
            "queries": 2,
        })

    def test_rejects_rule_id_outside_dataset(self) -> None:
        documents = [{"query": {"text": "bad"}, "hits": [{"ruleId": 5}]}]
        with self.assertRaisesRegex(ValueError, "outside dataset"):
            COVERAGE.compile_report(documents, 5, "fixture")


if __name__ == "__main__":
    unittest.main()
