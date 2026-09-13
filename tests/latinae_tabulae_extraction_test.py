#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/extract_latinae_tabulae.py"
SPEC = importlib.util.spec_from_file_location("extract_latinae_tabulae", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load LatinaeTabulae extractor")
EXTRACTOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = EXTRACTOR
SPEC.loader.exec_module(EXTRACTOR)


class LatinaeTabulaeExtractionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workbook = EXTRACTOR.extract_workbook(ROOT / "LatinaeTabulae.ods")
        cls.sheets = {sheet["name"]: sheet for sheet in cls.workbook["sheets"]}

    def cell(self, sheet: str, reference: str) -> dict[str, object]:
        return next(
            cell
            for cell in self.sheets[sheet]["cells"]
            if cell["ref"] == reference
        )

    def test_extracts_the_reviewed_workbook_snapshot(self) -> None:
        self.assertEqual(self.workbook["format"], "words.latinae-tabulae-extract-1")
        self.assertEqual(
            self.workbook["source"]["sha256"],
            "d7bff31b9e2d01153549e34417653175c2e3dc7bd86f5434f6ffeea15ad288d2",
        )
        self.assertEqual(
            {
                name: (sheet["extent"], sheet["nonEmptyCellCount"])
                for name, sheet in self.sheets.items()
            },
            {
                "Declinationes": ({"rows": 15, "columns": 9}, 90),
                "Pronomina": ({"rows": 24, "columns": 19}, 215),
                "Verba": ({"rows": 16, "columns": 8}, 53),
                "Verba II": ({"rows": 6, "columns": 9}, 28),
            },
        )

    def test_preserves_coordinates_merges_and_quantity_marks(self) -> None:
        self.assertEqual(self.cell("Declinationes", "A2")["columnSpan"], 9)
        self.assertEqual(self.cell("Declinationes", "G7")["text"], "-ūs")
        self.assertEqual(
            self.cell("Declinationes", "G7")["explicitQuantities"],
            [{"index": 1, "quantity": "long"}],
        )
        self.assertEqual(self.cell("Pronomina", "B20")["text"], "egō")
        self.assertEqual(self.cell("Verba", "C3")["text"], "–ō, –m")
        self.assertEqual(
            self.cell("Verba II", "B4")["text"],
            "[ba] (amābam, amābās / capiēbam, capiēbās)",
        )

    def test_json_render_is_deterministic_and_unicode_preserving(self) -> None:
        first = EXTRACTOR.render(self.workbook)
        second = EXTRACTOR.render(
            EXTRACTOR.extract_workbook(ROOT / "LatinaeTabulae.ods")
        )
        self.assertEqual(first, second)
        self.assertEqual(json.loads(first), self.workbook)
        self.assertIn("amābās", first)
        self.assertNotIn("\\u0101", first)


if __name__ == "__main__":
    unittest.main()
