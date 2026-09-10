#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "whitakers-words/poc/compact-db/import_ada_rewrites.py"
SPEC = importlib.util.spec_from_file_location("import_ada_rewrites", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load rewrite importer")
IMPORTER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = IMPORTER
SPEC.loader.exec_module(IMPORTER)


class RewriteImportTest(unittest.TestCase):
    def test_repository_syncope_block_matches_reviewed_ada_routine(self) -> None:
        rewrites = (ROOT / "whitakers-words/REWRITES.LAT").read_text()
        IMPORTER.validate_syncope(ROOT / "whitakers-words", rewrites)
        self.assertEqual(len(IMPORTER.SYNCOPE_RECORDS), 11)

    def test_ada_syncope_drift_requires_review(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = ROOT / "whitakers-words/src/words_engine/words_engine-tricks.adb"
            target = root / "src/words_engine/words_engine-tricks.adb"
            target.parent.mkdir(parents=True)
            shutil.copyfile(source, target)
            target.write_text(target.read_text().replace(
                "procedure Syncope", "procedure Syncope_Changed", 1
            ))
            rewrites = (ROOT / "whitakers-words/REWRITES.LAT").read_text()
            with self.assertRaisesRegex(ValueError, "Ada Syncope routine drift"):
                IMPORTER.validate_syncope(root, rewrites)


if __name__ == "__main__":
    unittest.main()
