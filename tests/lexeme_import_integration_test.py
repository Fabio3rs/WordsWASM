#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


TEST_DIRECTORY = Path(__file__).resolve().parent
sys.path.insert(0, str(TEST_DIRECTORY))

from lexeme_compilation_test import COMPILE, candidate, decision


DATASET_ID = "sha256:" + "0" * 64
LEGACY_INPUTS = (
    "DICTFILE.GEN",
    "STEMFILE.GEN",
    "INFLECTS.SEC",
    "ADDONS.LAT",
    "UNIQUES.LAT",
    "REWRITES.LAT",
    "QUANTITIES.LAT",
)


def run_json(command: list[str]) -> dict:
    completed = subprocess.run(
        command,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--packer", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    arguments = parser.parse_args()

    source = arguments.root.resolve() / "whitakers-words"
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for name in LEGACY_INPUTS:
            (root / name).symlink_to(source / name)

        item = candidate("satagius")
        accepted = decision(item, "lexdecision:satagius:test")
        queue = root / "review.jsonl"
        ledger = root / "decisions.jsonl"
        queue.write_text(
            json.dumps(item, ensure_ascii=False, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        ledger.write_text(
            json.dumps(accepted, ensure_ascii=False, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        subprocess.run(
            [
                sys.executable,
                str(source / "poc/compact-db/compile_lexemes.py"),
                str(queue),
                str(ledger),
                "--output",
                str(root / "LEXEMES.LAT"),
                "--report",
                str(root / "compile-report.json"),
            ],
            check=True,
        )

        full_database = root / "full.wwdb"
        search_database = root / "search.wwdb"
        subprocess.run(
            [str(arguments.packer), str(root), str(full_database), "dense"],
            check=True,
        )
        subprocess.run(
            [str(arguments.packer), str(root), str(search_database), "search-only"],
            check=True,
        )

        full = run_json(
            [
                str(arguments.cli),
                "--database",
                str(full_database),
                "--dataset-id",
                DATASET_ID,
                "--format",
                "analysis",
                "--two-words=legacy",
                "satagius",
            ]
        )
        imported = [
            analysis
            for analysis in full["analyses"]
            if analysis["lexeme"]["entryId"] == 39340
        ]
        if full["status"] != "analyzed" or not imported:
            raise AssertionError("compiled lexeme was not analyzed from the full WWDB")
        lexeme = imported[0]["lexeme"]
        if lexeme["meaning"] != "synthetic imported adjective;":
            raise AssertionError("full WWDB lost the imported meaning")
        if lexeme["metadata"]["source"] != "other-dictionaries":
            raise AssertionError("full WWDB lost imported provenance")
        if imported[0]["derivation"]["method"] != "regular":
            raise AssertionError("imported lexeme did not take the regular exact path")
        if full.get("suggestions", []):
            raise AssertionError("exact imported lexeme did not suppress Two_Words")

        search = run_json(
            [
                str(arguments.cli),
                "--database",
                str(search_database),
                "--dataset-id",
                DATASET_ID,
                "--format",
                "search",
                "satagius",
            ]
        )
        if search["status"] != "analyzed" or not any(
            hit["lexemeId"] == 39339 for hit in search["hits"]
        ):
            raise AssertionError("search WWDB does not preserve the imported lexeme ID")

        collision = dict(COMPILE.compile_decision(accepted))
        collision.update(
            {
                "decision_id": "lexdecision:collision:puella",
                "stems": ["puell", "puell", "", ""],
                "part_of_speech": 1,
                "paradigm": 0x11,
                "class_payload": 2 | (6 << 3),
            }
        )
        (root / "LEXEMES.LAT").write_text(
            COMPILE.render_jsonl([collision]), encoding="utf-8"
        )
        collided = subprocess.run(
            [str(arguments.packer), str(root), str(root / "collision.wwdb"), "dense"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if collided.returncode == 0 or "collides with lexical entry" not in collided.stderr:
            raise AssertionError("packer did not reject an existing lexical structure")

        # WHY: the narrow profile is an explicit release choice. A generated
        # batch must fail before any packed offset or reference can wrap.
        template = COMPILE.compile_decision(accepted)
        overflow = []
        for index in range(3450):
            record = dict(template)
            record["decision_id"] = f"lexdecision:overflow:{index}"
            overflow.append(record)
        (root / "LEXEMES.LAT").write_text(
            COMPILE.render_jsonl(overflow), encoding="utf-8"
        )
        failed = subprocess.run(
            [str(arguments.packer), str(root), str(root / "overflow.wwdb"), "dense"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if failed.returncode == 0 or "u16 lexeme/reference capacity" not in failed.stderr:
            raise AssertionError("packer did not reject overflowing u16 references")


if __name__ == "__main__":
    main()
