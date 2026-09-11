#!/usr/bin/env python3
"""Executable regressions derived from reported Whitaker issues."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def run(cli: Path, database: Path, arguments: list[str], *, input_text: str = "") -> list[dict]:
    completed = subprocess.run(
        [
            str(cli),
            "--database",
            str(database),
            "--format",
            "search-v2",
            *arguments,
        ],
        check=True,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    return [json.loads(line) for line in completed.stdout.splitlines()]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cpp", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    cli = args.cpp.resolve()
    database = root / "whitakers-words/poc/compact-db/output/words-poc-dense.wwdb"

    direct = run(cli, database, ["bestiasviginti"])[0]
    if direct["status"] != "unknown":
        raise AssertionError("issue #70 direct query changed unexpectedly")

    recovered = run(cli, database, ["--two-words=legacy", "bestiasviginti"])[0]
    segments = recovered.get("suggestions", [{}])[0].get("segments", [])
    if [segment.get("text") for segment in segments] != ["bestias", "viginti"]:
        raise AssertionError("issue #70 did not preserve the bounded split")

    flagged = run(cli, database, ["audetur"])[0]
    incompatible = [
        hit
        for hit in flagged["hits"]
        if not hit["assessment"]["whitakerTrim"]["compatible"]
    ]
    if flagged["status"] != "analyzed" or not incompatible:
        raise AssertionError("trim-incompatible interpretation was not preserved")

    removed_filter = subprocess.run(
        [
            str(cli),
            "--database",
            str(database),
            "--format",
            "search-v2",
            "--whitaker-trim=filter",
            "audetur",
        ],
        input="",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    if removed_filter.returncode == 0:
        raise AssertionError("destructive trim option is still accepted")

    for issue in (76, 145):
        corpus = root / f"tests/corpora/issue-{issue}.txt"
        documents = run(
            cli,
            database,
            ["--batch-json-lines"],
            input_text=corpus.read_text(encoding="utf-8"),
        )
        expected = corpus.read_text(encoding="utf-8").splitlines()
        if len(documents) != len(expected):
            raise AssertionError(f"issue #{issue} lost queries")
        failures = [
            document["query"]["text"]
            for document in documents
            if document["status"] != "analyzed"
        ]
        if failures:
            raise AssertionError(f"issue #{issue} regressions: {failures!r}")


if __name__ == "__main__":
    main()
