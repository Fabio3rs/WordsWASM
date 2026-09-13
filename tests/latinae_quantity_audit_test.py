#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/audit_latinae_quantities.py"
SPEC = importlib.util.spec_from_file_location("audit_latinae_quantities", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load LatinaeTabulae quantity audit")
AUDIT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = AUDIT
SPEC.loader.exec_module(AUDIT)


class LatinaeQuantityAuditTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.report = AUDIT.compile_audit(ROOT)

    def test_all_direct_declension_claims_are_accounted_for(self) -> None:
        self.assertEqual(
            self.report["counts"]["directDeclensionStatuses"],
            {"exact": 130, "source-has-no-per-letter-quantity": 6},
        )
        self.assertNotIn(
            "missing", self.report["counts"]["directDeclensionStatuses"]
        )
        self.assertNotIn(
            "conflict", self.report["counts"]["directDeclensionStatuses"]
        )

    def test_reports_the_complete_dense_rule_inventory(self) -> None:
        self.assertEqual(self.report["counts"]["rules"], 1_785)
        self.assertEqual(self.report["counts"]["annotatedInflectionRules"], 131)
        self.assertEqual(self.report["counts"]["vowelBearingRules"], 1_714)
        self.assertEqual(
            self.report["counts"]["unannotatedVowelBearingRules"], 1_583
        )
        self.assertEqual(
            {
                part: values["missingRules"]
                for part, values in self.report["byPartOfSpeech"].items()
                if values["missingRules"]
            },
            {
                "noun": 106,
                "pronoun": 157,
                "adjective": 208,
                "numeral": 123,
                "verb": 642,
                "participle": 345,
                "supine": 2,
            },
        )

    def test_direct_claims_retain_exact_cell_provenance(self) -> None:
        fourth_genitive = next(
            comparison
            for comparison in self.report["directDeclensionComparisons"]
            if comparison["rule"]["ruleId"] == 1_513
        )
        self.assertEqual(fourth_genitive["expected"]["marked"], "ūs")
        self.assertEqual(fourth_genitive["actual"]["marked"], "ūs")
        self.assertEqual(fourth_genitive["sources"][0]["cell"], "G7")
        self.assertEqual(fourth_genitive["sources"][0]["text"], "-ūs")

        first_nominative = next(
            comparison
            for comparison in self.report["directDeclensionComparisons"]
            if comparison["rule"]["ruleId"] == 148
        )
        self.assertEqual(first_nominative["expected"]["marked"], "ă")
        self.assertEqual(
            first_nominative["sources"][0]["quantities"],
            [{"position": 0, "quantity": "short", "origin": "inferred-short"}],
        )

    def test_explicit_pronominal_marks_are_projected_but_not_invented(self) -> None:
        self.assertEqual(
            self.report["counts"]["explicitPronominalStatuses"], {"missing": 81}
        )
        self.assertEqual(
            self.report["counts"]["explicitPronominalFormsInLexicalStem"], 3
        )
        projected = {
            comparison["rule"]["ruleId"]: comparison
            for comparison in self.report["explicitPronominalComparisons"]
        }
        self.assertEqual(projected[1_233]["expected"]["marked"], "ōbīs")
        self.assertEqual(
            {source["cell"] for source in projected[1_233]["sources"]},
            {"C22", "E22"},
        )
        lexical = {
            (item["cell"], item["form"])
            for item in self.report["explicitPronominalLexicalForms"]
        }
        self.assertIn(("B20", "ego"), lexical)
        self.assertIn(("D20", "tu"), lexical)

    def test_core_audit_has_no_dependency_on_untracked_study_material(self) -> None:
        self.assertEqual(self.report["sources"]["comparisonGrammars"], [])
        self.assertEqual(self.report["workbookIssues"], [])

    @unittest.skipUnless((ROOT / ".study").is_dir(), "local .study corpus absent")
    def test_local_citations_are_bibliographic_and_searchable(self) -> None:
        report = AUDIT.compile_audit(ROOT, ROOT / ".study")
        sources = {
            source["id"]: source
            for source in report["sources"]["comparisonGrammars"]
        }
        self.assertIsNone(sources["dooge-latin-for-beginners-18251"]["isbn"])
        self.assertEqual(
            sources["almeida-gramatica-latina-29"]["isbn10"], "85-02-00307-0"
        )
        self.assertEqual(
            sources["almeida-gramatica-latina-29"]["isbn13"],
            "978-85-02-00307-1",
        )
        for source in sources.values():
            self.assertTrue(source["author"])
            self.assertTrue(source["title"])
            self.assertTrue(source["url"].startswith("https://"))
        for issue in report["workbookIssues"]:
            for confirmation in issue["confirmation"]:
                self.assertIn(confirmation["sourceId"], sources)
                self.assertTrue(confirmation["locator"])
                self.assertTrue(confirmation["expression"])
                self.assertNotRegex(confirmation["locator"], r"\.html:\d+")


if __name__ == "__main__":
    unittest.main()
