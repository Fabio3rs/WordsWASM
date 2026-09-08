#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import argparse
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPACT_DB = ROOT / "whitakers-words/poc/compact-db"
sys.path.insert(0, str(COMPACT_DB))
SCRIPT = COMPACT_DB / "dump_lexical_comparison.py"
SPEC = importlib.util.spec_from_file_location("dump_lexical_comparison", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load lexical comparison dumper")
DUMP = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = DUMP
SPEC.loader.exec_module(DUMP)


def source_entry(source: str, identifier: str, lemma: str, part: str, gender: str | None = None) -> DUMP.SourceEntry:
    return DUMP.SourceEntry(
        source, identifier, lemma, part, part, gender, gender, None, False,
        {}, {}, {}, identifier,
    )


class LexicalComparisonDumpTest(unittest.TestCase):
    def test_source_manifest_hashes_files_and_sqlite_schema_read_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            dictionary = root / "DICTFILE.GEN"
            dictionary.write_bytes(b"fixture")
            database = root / "ls.db"
            connection = sqlite3.connect(database)
            connection.execute("create table entry(id integer primary key, lemma text)")
            connection.commit()
            connection.close()
            arguments = argparse.Namespace(
                dictionary=dictionary,
                quantities=None,
                ls_database=database,
                gaffiot_database=None,
                faria_v3=None,
                latin_german=None,
            )

            manifest = DUMP.source_manifest(arguments)

            self.assertEqual([item["source_key"] for item in manifest], ["words_dictionary", "ls_dict"])
            self.assertNotIn("schema_signature", manifest[0])
            self.assertRegex(manifest[1]["schema_signature"], r"^sha256:[0-9a-f]{64}$")
            self.assertEqual(
                sqlite3.connect(f"file:{database}?mode=ro&immutable=1", uri=True)
                .execute("select count(*) from entry")
                .fetchone()[0],
                0,
            )

    def test_does_not_infer_adjective_gender_from_head_abbreviations(self) -> None:
        self.assertIsNone(DUMP.contextual_gender("ADJ", None, "levis, adj.; Cic. N. D."))
        self.assertEqual(DUMP.contextual_gender(None, None, "malum, i, n."), "n")

    def test_majority_is_per_family_and_per_vowel_position(self) -> None:
        entries = [
            source_entry("ls_dict", "ls", "mālum", "NOUN", "n"),
            source_entry("gaffiot", "g", "mālum", "NOUN", "n"),
            source_entry("faria_v3", "f", "mălum", "NOUN", "n"),
        ]
        preview = DUMP.consensus_preview(entries)[0]

        self.assertEqual(preview["status"], "eligible_unique_entry_per_family")
        self.assertEqual(preview["fields"]["gender"]["status"], "majority_2_of_3")
        self.assertEqual(preview["vowel_quantity"][0]["status"], "majority_2_of_3")
        self.assertEqual(preview["vowel_quantity"][0]["value"], "long")
        self.assertFalse(preview["automatic_promotion_allowed"])

    def test_same_source_homographs_block_consensus(self) -> None:
        entries = [
            source_entry("ls_dict", "apple", "mālum", "NOUN", "n"),
            source_entry("ls_dict", "evil", "mălum", "NOUN", "n"),
            source_entry("gaffiot", "g", "mālum", "NOUN", "n"),
            source_entry("faria_v3", "f", "mālum", "NOUN", "n"),
        ]

        preview = DUMP.consensus_preview(entries)[0]

        self.assertEqual(preview["status"], "blocked_on_semantic_alignment")
        self.assertEqual(preview["multiple_entries_by_family"], {"lewis": 2})
        self.assertNotIn("fields", preview)

    def test_untyped_witness_joins_the_only_known_part(self) -> None:
        entries = [
            source_entry("ls_dict", "ls", "ămō", "VERB"),
            source_entry("gaffiot", "g", "ămō", None),
        ]

        preview = DUMP.consensus_preview(entries)[0]

        self.assertEqual(preview["part_of_speech"], "VERB")
        self.assertEqual(preview["status"], "eligible_unique_entry_per_family")

    def test_unmarked_vowels_do_not_vote_short(self) -> None:
        entries = [
            source_entry("ls_dict", "ls", "malum", "NOUN"),
            source_entry("gaffiot", "g", "mālum", "NOUN"),
            source_entry("faria_v3", "f", "mālum", "NOUN"),
        ]

        vote = DUMP.consensus_preview(entries)[0]["vowel_quantity"][0]

        self.assertEqual(vote["status"], "majority_2_of_3")
        self.assertEqual(vote["votes"], {"faria": "long", "gaffiot": "long"})


if __name__ == "__main__":
    unittest.main()
