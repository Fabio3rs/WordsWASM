#!/usr/bin/env python3
"""Executable regressions derived from reported Whitaker issues."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def run(cli: Path, database: Path, arguments: list[str], *,
        input_text: str = "", cwd: Path | None = None) -> list[dict]:
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
        cwd=cwd,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    return [json.loads(line) for line in completed.stdout.splitlines()]


def one(documents: list[dict], surface: str) -> dict:
    if len(documents) != 1 or documents[0]["query"]["text"] != surface:
        raise AssertionError(f"expected exactly one response for {surface!r}")
    return documents[0]


def check_corpus(documents: list[dict], expected: list[str], issue: int) -> None:
    actual = [document["query"]["text"] for document in documents]
    if actual != expected:
        raise AssertionError(f"issue #{issue} changed query count or order")
    for surface, document in zip(expected, documents, strict=True):
        if document["status"] != "analyzed" or not document["hits"]:
            raise AssertionError(f"issue #{issue} did not analyze {surface!r}")
        for hit in document["hits"]:
            if any(type(hit[key]) is not int or hit[key] < 0
                   for key in ("lexemeId", "ruleId")):
                raise AssertionError(f"issue #{issue} invalid IDs for {surface!r}")
            if any(type(addon) is not int or addon < 0
                   for addon in hit["addonIds"]):
                raise AssertionError(f"issue #{issue} invalid addon ID for {surface!r}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cpp", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    cli = args.cpp.resolve()
    output = root / "whitakers-words/poc/compact-db/output"
    databases = {
        "dense": output / "words-poc-dense.wwdb",
        "search-only": output / "words-poc-search-only.wwdb",
    }
    results: dict[str, dict[str, object]] = {}
    with tempfile.TemporaryDirectory() as temporary:
        # All paths are absolute; an unrelated cwd and closed stdin exercise
        # the CLI contract without relying on the checkout's data files.
        cwd = Path(temporary)
        for profile, database in databases.items():
            profile_results: dict[str, object] = {}
            direct = one(run(cli, database, ["bestiasviginti"], cwd=cwd),
                         "bestiasviginti")
            if direct["status"] != "unknown":
                raise AssertionError("issue #70 direct query changed unexpectedly")
            profile_results["direct"] = direct

            recovered = one(run(cli, database,
                                ["--two-words=legacy", "bestiasviginti"],
                                cwd=cwd), "bestiasviginti")
            segments = recovered.get("suggestions", [{}])[0].get("segments", [])
            if [segment.get("text") for segment in segments] != ["bestias", "viginti"]:
                raise AssertionError("issue #70 did not preserve the bounded split")
            profile_results["recovered"] = recovered

            flagged = one(run(cli, database, ["audetur"], cwd=cwd), "audetur")
            incompatible = [
                hit for hit in flagged["hits"]
                if not hit["assessment"]["whitakerTrim"]["compatible"]
            ]
            if flagged["status"] != "analyzed" or not incompatible:
                raise AssertionError("trim-incompatible interpretation was not preserved")
            profile_results["flagged"] = flagged

            removed_filter = subprocess.run(
                [str(cli), "--database", str(database), "--format", "search-v2",
                 "--whitaker-trim=filter", "audetur"],
                cwd=cwd, stdin=subprocess.DEVNULL, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10,
            )
            if removed_filter.returncode == 0:
                raise AssertionError("destructive trim option is still accepted")

            for surface, expected in (("canis", ["canis"]),
                                      ("canis canis", ["canis", "canis"])):
                documents = run(cli, database, [surface], cwd=cwd)
                if ([document["query"]["text"] for document in documents]
                        != expected or
                        any(document["status"] != "analyzed" for document in documents)):
                    raise AssertionError(f"CLI lost tokens for {surface!r}")
                profile_results[surface] = documents

            for issue in (76, 145):
                corpus = root / f"tests/corpora/issue-{issue}.txt"
                input_text = corpus.read_text(encoding="utf-8")
                documents = run(cli, database, ["--batch-json-lines"],
                                input_text=input_text, cwd=cwd)
                check_corpus(documents, input_text.splitlines(), issue)
                profile_results[f"issue-{issue}"] = documents
            results[profile] = profile_results

    for case, dense in results["dense"].items():
        if dense != results["search-only"][case]:
            raise AssertionError(f"full and search-only results differ for {case}")


if __name__ == "__main__":
    main()
