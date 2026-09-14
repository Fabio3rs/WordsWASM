#!/usr/bin/env python3

from __future__ import annotations

import csv
import gzip
import importlib.util
import json
import os
import sqlite3
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPACT = ROOT / "whitakers-words/poc/compact-db"


def load_module(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, COMPACT / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    module = importlib.util.module_from_spec(spec)
    import sys

    sys.path.insert(0, str(COMPACT))
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


EXTRACT = load_module("extract_tll_word_frequencies", "extract_tll_word_frequencies.py")
RANK = load_module("rank_tll_unknown_words", "rank_tll_unknown_words.py")


class TllFrequencyTest(unittest.TestCase):
    def create_tll(self, path: Path) -> None:
        connection = sqlite3.connect(path)
        connection.executescript(
            """
            CREATE TABLE processing_run(
                id INTEGER PRIMARY KEY,run_type TEXT,pipeline_name TEXT,
                pipeline_version TEXT,config_sha256 TEXT,config_json TEXT,
                status TEXT,is_active INTEGER,started_at TEXT,finished_at TEXT
            );
            CREATE TABLE text_unit(
                id INTEGER PRIMARY KEY,normalized_text TEXT,is_canonical INTEGER
            );
            CREATE TABLE nlp_selection(
                id INTEGER PRIMARY KEY,processing_run_id INTEGER,text_unit_id INTEGER,
                source_artifact_id INTEGER,sequence_no INTEGER,decision TEXT
            );
            INSERT INTO processing_run VALUES(
                12,'nlp_prepare','tll-corpus','1','abc','{}','completed',1,'a','b'
            );
            INSERT INTO text_unit VALUES(1,'Et āmō. AMO Israël',1);
            INSERT INTO text_unit VALUES(2,'et d’Artagnan',1);
            INSERT INTO text_unit VALUES(3,'ignored',1);
            INSERT INTO text_unit VALUES(4,'noncanonical',0);
            INSERT INTO nlp_selection VALUES(1,12,1,8,0,'include');
            INSERT INTO nlp_selection VALUES(2,12,2,9,0,'include');
            INSERT INTO nlp_selection VALUES(3,12,3,9,1,'exclude');
            INSERT INTO nlp_selection VALUES(4,12,4,9,2,'include');
            """
        )
        connection.commit()
        connection.close()

    def test_extracts_selected_canonical_forms_and_artifact_frequency(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            database = Path(name) / "tll.sqlite3"
            self.create_tll(database)
            connection = EXTRACT.readonly_connection(database)
            try:
                run = EXTRACT.active_selection_run(connection)
                forms, counts = EXTRACT.extract_frequencies(connection, int(run["id"]))
                with self.assertRaises(sqlite3.OperationalError):
                    connection.execute("CREATE TABLE forbidden(value)")
            finally:
                connection.close()

            self.assertEqual(counts, {
                "selected_units": 2,
                "selected_artifacts": 2,
                "tokens": 6,
                "distinct_forms": 5,
            })
            self.assertEqual(forms["et"].occurrences, 2)
            self.assertEqual(forms["et"].artifact_count, 2)
            self.assertEqual(forms["āmō"].followed_by_period_occurrences, 1)
            self.assertEqual(forms["amo"].all_upper_occurrences, 1)
            self.assertIn("d’artagnan", forms)

    def test_frequency_gzip_is_byte_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            stats = EXTRACT.FormStats(occurrences=2, artifact_count=1)
            stats.variants.update({"Et": 1, "et": 1})
            first = root / "first.tsv.gz"
            second = root / "second.tsv.gz"
            EXTRACT.write_frequencies(first, {"et": stats})
            EXTRACT.write_frequencies(second, {"et": stats})
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with gzip.open(first, "rt", encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(rows[0]["form"], "et")
            self.assertEqual(json.loads(rows[0]["variants_json"])[0]["surface"], "Et")


class TllRankingTest(unittest.TestCase):
    def fake_cli(self, path: Path, *, extra: bool = False) -> None:
        suffix = "print(json.dumps(document))" if extra else ""
        path.write_text(
            "#!/usr/bin/env python3\n"
            "import json,sys\n"
            "for word in sys.stdin:\n"
            " word=word.rstrip('\\n')\n"
            " status='unknown' if word.startswith('x') or word=='respublica' else "
            "('error' if word.startswith('!') else 'analyzed')\n"
            " document={'schema':'whitakers-words.search','query':{'text':word},"
            "'status':status,'hits':([{'lexemeId':7}] if status=='analyzed' else []),"
            "'diagnostics':([{'code':'bad'}] if status=='error' else [])}\n"
            " if word=='respublica' and '--two-words=legacy' in sys.argv:\n"
            "  document['suggestions']=[{'method':'two-words','splitAt':3,"
            "'classification':'classical',"
            "'segments':[{'text':'res','hits':[{'lexemeId':11}]},"
            "{'text':'publica','hits':[{'lexemeId':12}]}]}]\n"
            " print(json.dumps(document))\n"
            f"{suffix}\n",
            encoding="utf-8",
        )
        path.chmod(0o755)

    def test_streaming_words_contract(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            cli = root / "fake.py"
            database = root / "words.wwdb"
            database.write_bytes(b"fixture")
            self.fake_cli(cli)
            result = RANK.classify_with_words(
                ["amo", "xeno", "!bad"], cli, database, "sha256:" + "0" * 64
            )
            self.assertEqual(result["amo"].status, "analyzed")
            self.assertEqual(result["amo"].lexeme_ids, (7,))
            self.assertEqual(result["xeno"].status, "unknown")
            self.assertEqual(result["!bad"].diagnostic_codes, ("bad",))

    def test_rejects_extra_cli_results(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            cli = root / "fake.py"
            database = root / "words.wwdb"
            database.write_bytes(b"fixture")
            self.fake_cli(cli, extra=True)
            with self.assertRaisesRegex(RANK.RankingError, "more results"):
                RANK.classify_with_words(
                    ["amo"], cli, database, "sha256:" + "0" * 64
                )

    def test_complete_two_word_analysis_is_effective_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            cli = root / "fake.py"
            database = root / "words.wwdb"
            database.write_bytes(b"fixture")
            self.fake_cli(cli)
            result = RANK.classify_with_words(
                ["respublica"],
                cli,
                database,
                "sha256:" + "0" * 64,
                two_words=True,
            )["respublica"]
            self.assertEqual(result.status, "unknown")
            self.assertEqual(result.two_words_suggestions[0]["split_at"], 3)
            self.assertEqual(
                result.two_words_suggestions[0]["segments"][1]["lexeme_ids"],
                [12],
            )
            frequency = RANK.Frequency("respublica", 10, 2, 0, 0, 0, "[]")
            self.assertEqual(
                RANK.queue_for(frequency, set(), None, True), "covered_two_words"
            )

    def test_normalization_and_queue_policy(self) -> None:
        self.assertEqual(RANK.strip_quantity_marks("mālŭm"), "malum")
        self.assertEqual(RANK.ascii_lookup("cœur"), "coeur")
        self.assertIsNone(RANK.ascii_lookup("λόγος"))
        frequency = RANK.Frequency("abbr", 10, 2, 0, 0, 8, "[]")
        self.assertEqual(
            RANK.queue_for(frequency, set(), None), "abbreviation_or_editorial"
        )
        proper = {("ulpianus", "NOUN", True)}
        proper_frequency = RANK.Frequency("ulpianus", 10, 2, 10, 0, 0, "[]")
        self.assertEqual(
            RANK.queue_for(proper_frequency, proper, None), "proper_name"
        )
        short = RANK.Frequency("um", 10, 2, 0, 0, 0, "[]")
        ambiguous = {("a", "NOUN", False), ("b", "NOUN", False)}
        self.assertEqual(
            RANK.queue_for(short, ambiguous, None), "abbreviation_or_editorial"
        )

    def test_external_form_mapping_preserves_ambiguity(self) -> None:
        first = {
            "ascii_lemma": "malum",
            "part_of_speech": "NOUN",
            "proper": False,
            "witnesses": [
                {"source": "gaffiot", "source_entry_id": "1"},
            ],
        }
        second = {
            "ascii_lemma": "malum",
            "part_of_speech": "ADJ",
            "proper": False,
            "witnesses": [
                {"source": "gaffiot", "source_entry_id": "2"},
            ],
        }
        candidates, references, lemmas = RANK.evidence_maps([first, second])
        self.assertEqual(len(candidates), 2)
        self.assertEqual(references[("gaffiot", "1")], {("malum", "NOUN", False)})
        self.assertEqual(len(lemmas["malum"]), 2)


if __name__ == "__main__":
    unittest.main()
