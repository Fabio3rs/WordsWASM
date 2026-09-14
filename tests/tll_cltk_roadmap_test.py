#!/usr/bin/env python3

from __future__ import annotations

import csv
import gzip
import importlib.util
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
COMPACT = ROOT / "whitakers-words/poc/compact-db"


def load_module(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, COMPACT / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(COMPACT))
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


ROADMAP = load_module(
    "enrich_tll_unknowns_with_cltk", "enrich_tll_unknowns_with_cltk.py"
)
EXTRACT = sys.modules["extract_tll_word_frequencies"]


class TllCltkRoadmapTest(unittest.TestCase):
    def create_tll(self, path: Path, *, input_run: int = 12) -> None:
        connection = sqlite3.connect(path)
        connection.executescript(
            """
            CREATE TABLE processing_run(
                id INTEGER PRIMARY KEY,run_type TEXT,pipeline_name TEXT,
                pipeline_version TEXT,status TEXT,is_active INTEGER,
                config_sha256 TEXT,config_json TEXT,started_at TEXT,finished_at TEXT
            );
            CREATE TABLE processing_run_input(
                processing_run_id INTEGER,input_run_id INTEGER,role TEXT,sequence_no INTEGER
            );
            CREATE TABLE cltk_run(
                id INTEGER PRIMARY KEY,processing_run_id INTEGER,cltk_version TEXT,
                language TEXT,pipeline_name TEXT,model_manifest_json TEXT
            );
            CREATE TABLE cltk_document(
                id INTEGER PRIMARY KEY,cltk_run_id INTEGER,sequence_no INTEGER
            );
            CREATE TABLE cltk_sentence(
                id INTEGER PRIMARY KEY,cltk_document_id INTEGER,sequence_no INTEGER
            );
            CREATE TABLE cltk_token(
                id INTEGER PRIMARY KEY,cltk_sentence_id INTEGER,sequence_no INTEGER,
                surface TEXT,lemma TEXT,upos TEXT,text_unit_id INTEGER,
                unit_char_start INTEGER,unit_char_end INTEGER
            );
            INSERT INTO processing_run VALUES(
                13,'cltk_pilot','fixture','1','completed',0,'sha','{}','a','b'
            );
            INSERT INTO cltk_run VALUES(
                1,13,'1.5.0','lat','stanza-ittb','{"treebank":"ittb"}'
            );
            INSERT INTO cltk_document VALUES(1,1,0);
            INSERT INTO cltk_document VALUES(2,1,1);
            INSERT INTO cltk_sentence VALUES(1,1,0);
            INSERT INTO cltk_sentence VALUES(2,2,0);
            INSERT INTO cltk_token VALUES(1,1,0,'Quendam','quīdam','DET',1,0,7);
            INSERT INTO cltk_token VALUES(2,1,1,'Cain','Cainus','PROPN',1,8,12);
            INSERT INTO cltk_token VALUES(3,1,2,'saram','Sara','NOUN',1,13,18);
            INSERT INTO cltk_token VALUES(4,2,0,'saram','Sarus','ADJ',2,0,5);
            INSERT INTO cltk_token VALUES(5,2,1,'reviewform.','Revius','VERB',2,6,17);
            INSERT INTO cltk_token VALUES(6,2,2,'badform','?','X',2,18,25);
            """
        )
        connection.execute(
            "INSERT INTO processing_run_input VALUES(13,?,'nlp_preparation',0)",
            (input_run,),
        )
        connection.commit()
        connection.close()

    def create_superdb(self, path: Path) -> None:
        connection = sqlite3.connect(path)
        connection.executescript(
            """
            CREATE TABLE source(id INTEGER PRIMARY KEY,name TEXT);
            CREATE TABLE entry(
                id INTEGER PRIMARY KEY,source_id INTEGER,source_entry_id TEXT,
                lemma TEXT,lemma_norm TEXT,pos_std TEXT,needs_review INTEGER,
                morph_class_std TEXT,head_raw TEXT
            );
            INSERT INTO source VALUES(1,'ls_dict');
            INSERT INTO source VALUES(2,'gaffiot');
            INSERT INTO source VALUES(3,'retificado_v2');
            INSERT INTO entry VALUES(1,1,'ls-quidam','quīdam','quidam','PRON',0,'','');
            INSERT INTO entry VALUES(2,2,'gaf-quidam','quīdam','quidam','ADJ',0,'','');
            INSERT INTO entry VALUES(3,1,'ls-cain','Caīnus','cainus','NOUN',0,'','');
            INSERT INTO entry VALUES(4,3,'stale','Sarus','sarus','ADJ',0,'','');
            """
        )
        connection.commit()
        connection.close()

    def create_faria(self, path: Path) -> None:
        connection = sqlite3.connect(path)
        connection.executescript(
            """
            CREATE TABLE entry(
                entry_id INTEGER PRIMARY KEY,id TEXT,lemma TEXT,lemma_sort TEXT,pos TEXT,
                conf TEXT,needs_review INTEGER,redirect_only INTEGER,
                morph_out_of_vocab INTEGER,morph_render TEXT
            );
            INSERT INTO entry VALUES(
                1,'faria-sara','Sara','sara','NOUN','conf:high',0,0,0,'s. f.'
            );
            INSERT INTO entry VALUES(
                2,'faria-review','Revius','revius','NOUN','conf:low',1,1,1,'s. m.'
            );
            """
        )
        connection.commit()
        connection.close()

    def create_latin_german(self, path: Path) -> None:
        connection = sqlite3.connect(path)
        connection.execute(
            "CREATE TABLE VOC(id INTEGER PRIMARY KEY,vok_id TEXT,latin TEXT,"
            "desc TEXT,grammar TEXT,typnr INTEGER)"
        )
        connection.execute(
            "INSERT INTO VOC VALUES(1,'lg-sara','Sara -ae, f','Sara','s',201)"
        )
        connection.commit()
        connection.close()

    def write_stage_one(
        self, root: Path, tll: Path
    ) -> tuple[Path, Path, Path]:
        unknown = root / "unknown.tsv.gz"
        fields = (
            "form", "occurrences", "artifact_count", "coverage_status", "queue"
        )
        rows = [
            ("quendam", 900, 50, "unknown", "common_lexeme"),
            ("cain", 30, 4, "unknown", "proper_name"),
            ("saram", 20, 3, "unknown", "common_lexeme"),
            ("reviewform", 10, 2, "unknown", "common_lexeme"),
            ("badform", 5, 1, "unknown", "unresolved"),
        ]
        with gzip.open(unknown, "wt", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
            writer.writerow(fields)
            writer.writerows(rows)
        candidates = root / "structural.jsonl.gz"
        structural = {
            "schema": "whitakers-words.tll-lexeme-priority.v1",
            "key": {"ascii_lemma": "quidam", "part_of_speech": "ADJ", "proper": False},
        }
        with gzip.open(candidates, "wt", encoding="utf-8") as stream:
            stream.write(json.dumps(structural) + "\n")
        report = root / "stage-one.json"
        report.write_text(
            json.dumps(
                {
                    "schema": "whitakers-words.tll-corpus-coverage-report.v1",
                    "corpus": {
                        "selection_run": {"id": 12},
                        "source": {"sha256": EXTRACT.file_digest(tll)},
                    },
                    "outputs": {
                        "unknown": {"sha256": EXTRACT.file_digest(unknown)},
                        "candidates": {"sha256": EXTRACT.file_digest(candidates)},
                    },
                }
            ),
            encoding="utf-8",
        )
        return unknown, candidates, report

    def fixture(self, root: Path) -> dict[str, Path]:
        tll = root / "tll.sqlite"
        superdb = root / "super.sqlite"
        faria = root / "faria.sqlite"
        german = root / "german.sqlite"
        self.create_tll(tll)
        self.create_superdb(superdb)
        self.create_faria(faria)
        self.create_latin_german(german)
        unknown, candidates, report = self.write_stage_one(root, tll)
        return {
            "tll": tll, "superdb": superdb, "faria": faria, "german": german,
            "unknown": unknown, "candidates": candidates, "report": report,
        }

    def run_main(self, paths: dict[str, Path], output: Path) -> None:
        arguments = [
            "enrich_tll_unknowns_with_cltk.py", str(paths["unknown"]),
            "--stage-one-report", str(paths["report"]),
            "--structural-candidates", str(paths["candidates"]),
            "--tll-database", str(paths["tll"]), "--cltk-run-id", "13",
            "--superdb", str(paths["superdb"]),
            "--latin-german", str(paths["german"]),
            "--retificado-v2", str(paths["faria"]),
            "--output-directory", str(output),
        ]
        with patch.object(sys, "argv", arguments):
            self.assertEqual(ROADMAP.main(), 0)

    def test_end_to_end_preserves_ambiguity_quality_and_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            paths = self.fixture(root)
            output = root / "output"
            self.run_main(paths, output)

            report = json.loads(
                (output / "tll-cltk-coverage-report.json").read_text(encoding="utf-8")
            )
            self.assertEqual(report["cltk_run"]["run_type"], "cltk_pilot")
            self.assertEqual(report["counts"]["unknown_forms_observed_in_pilot"], 5)
            self.assertFalse(report["policy"]["automatic_promotion_allowed"])

            with gzip.open(
                output / "tll-cltk-lexeme-priorities.jsonl.gz", "rt", encoding="utf-8"
            ) as stream:
                candidates = [json.loads(line) for line in stream]
            by_lemma = {row["key"]["lemma"]: row for row in candidates}
            self.assertEqual(
                by_lemma["quidam"]["dictionary_evidence"]["support"],
                "corroborated_independent",
            )
            self.assertTrue(by_lemma["quidam"]["structural_stage_candidates"])
            self.assertEqual(by_lemma["cainus"]["roadmap_queue"], "proper_name")
            self.assertEqual(by_lemma["sara"]["corpus"]["ambiguous_form_count"], 1)
            self.assertEqual(
                by_lemma["revius"]["roadmap_queue"], "model_or_pos_review"
            )
            self.assertEqual(
                by_lemma["revius"]["dictionary_evidence"]["entries"][0]["quality"],
                "conf:low",
            )
            self.assertNotIn("respublica", by_lemma)
            sources = by_lemma["sara"]["dictionary_evidence"]["sources"]
            self.assertEqual(sources, ["faria-v2-retificado", "latin-german"])

    def test_outputs_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            paths = self.fixture(root)
            output = root / "output"
            self.run_main(paths, output)
            names = (
                "tll-cltk-lemma-evidence.tsv.gz",
                "tll-cltk-lexeme-priorities.jsonl.gz",
                "tll-cltk-roadmap.tsv",
            )
            before = {item: (output / item).read_bytes() for item in names}
            self.run_main(paths, output)
            self.assertEqual(
                before, {item: (output / item).read_bytes() for item in names}
            )

    def test_rejects_cltk_run_from_another_selection(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            database = Path(name) / "tll.sqlite"
            self.create_tll(database, input_run=11)
            connection = EXTRACT.readonly_connection(database)
            try:
                with self.assertRaisesRegex(
                    ROADMAP.CltkRoadmapError, "does not use the stage-one"
                ):
                    ROADMAP.cltk_run_metadata(connection, 13, 12)
                with self.assertRaises(sqlite3.OperationalError):
                    connection.execute("CREATE TABLE forbidden(value)")
            finally:
                connection.close()

    def test_normalization_and_pos_are_non_destructive(self) -> None:
        self.assertEqual(ROADMAP.normalize_lemma("rēs-pūblica"), None)
        self.assertEqual(ROADMAP.normalize_lemma("quīdam"), "quidam")
        self.assertEqual(ROADMAP.token_form("Quendam."), "quendam")
        self.assertEqual(ROADMAP.assess_pos("PROPN", "NOUN"), "compatible")
        self.assertEqual(ROADMAP.assess_pos("VERB", "NOUN"), "conflict")
        self.assertEqual(ROADMAP.assess_pos("VERB", None), "unknown")


if __name__ == "__main__":
    unittest.main()
