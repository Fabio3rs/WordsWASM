#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from pathlib import Path

import jsonschema


DATASET_ID = "sha256:" + "0" * 64
NOUN_FIXTURES = ("puella", "servus", "regina", "rex", "manus", "dies")
ADJECTIVE_FIXTURES = (
    "pulcher", "bonus", "maior", "maximus", "acer", "bellum", "fortis",
)
DERIVED_FIXTURES = ("anaticulus", "anaticuliculus")
PREFIX_FIXTURES = ("archipuella", "appuella", "inbonus", "archipuellulus")
TACKON_FIXTURES = (
    "puellaque", "anaticulusque", "archipuellaque", "mecum",
)
PACKON_FIXTURES = ("quidam", "quispiam", "ecquidam", "ecquis")
SEMANTIC_FIXTURES = (
    "quis", "unus", "duo", "bene", "fortiter", "amo", "amas", "amavi",
    "amans", "amantem", "amatum", "cum", "sine", "et", "atque", "heu",
    "eheu", "rosa", "rosae", "forte",
)
DERIVED_SEMANTIC_FIXTURES = ("amesco", "boniter", "binteni")
UNIQUE_FIXTURES = (
    "mavis", "eadem", "iusiurandum", "ec", "nonus", "exspes", "mavisque",
)
# mi remains covered by the native homograph test; its Ada envelope also
# contains an unrelated historical UNIQUE+suffix association tracked with the
# remaining addon compatibility decisions.
ROMAN_FIXTURES = ("iv", "IIII", "di", "dii", "iiv", "ivque")
SYNCOPE_FIXTURES = ("amasti", "amarunt")
ORTHOGRAPHIC_FIXTURES = (
    "pretor", "eclesia", "philosofus", "teologia", "literatura", "obpono",
    "ahmasti", "propris", "pretoribusque", "obponoque",
)
COMPOUND_FIXTURES = (
    "amata est", "amata erat", "amata erit", "amata esset",
    "amata fuerit", "amata fuerunt", "amati sunt", "amaturus est",
    "amatus esse", "amaturus esse", "amaturus fuisse", "amatum iri",
)
TWO_WORD_FIXTURES = {
    "respublica": ("res", "publica"),
    "annam": ("an", "nam"),
    "orion": ("or", "ion"),
    "unustres": ("unus", "tres"),
}
UNKNOWN_FIXTURES = ("anaticuliculiculus", "archiarchipuella")


def load_json(command: list[str], cwd: Path | None = None) -> dict:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cpp", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    ada_root = root / "whitakers-words"
    database = ada_root / "poc/compact-db/output/words-poc-dense.wwdb"
    analysis_schema = json.loads((root / "schemas/analysis-v1.schema.json").read_text())
    search_schema = json.loads((root / "schemas/search-v1.schema.json").read_text())

    subprocess.run([
        sys.executable,
        str(ada_root / "poc/compact-db/import_ada_rewrites.py"),
        str(ada_root),
        "--check", str(ada_root / "REWRITES.LAT"),
    ], check=True)

    def cpp(word: str, output_format: str,
            two_words: bool = False) -> dict:
        command = [
            str(args.cpp),
            "--database", str(database),
            "--dataset-id", DATASET_ID,
            "--format", output_format,
        ]
        if two_words:
            command.append("--two-words=legacy")
        command.append(word)
        return load_json(command)

    for word in (
        NOUN_FIXTURES + ADJECTIVE_FIXTURES + DERIVED_FIXTURES + PREFIX_FIXTURES
        + TACKON_FIXTURES + PACKON_FIXTURES + SEMANTIC_FIXTURES
        + DERIVED_SEMANTIC_FIXTURES + UNIQUE_FIXTURES
        + ROMAN_FIXTURES
    ):
        expected = load_json(["bin/words_json", word], cwd=ada_root)
        actual = cpp(word, "analysis")
        if actual != expected:
            raise AssertionError(f"C++ differs from Ada for {word}")
        jsonschema.validate(actual, analysis_schema)
        jsonschema.validate(cpp(word, "search"), search_schema)

    for word in UNKNOWN_FIXTURES:
        expected = load_json(["bin/words_json", word], cwd=ada_root)
        actual = cpp(word, "analysis")
        if actual != expected:
            raise AssertionError(f"C++ differs from Ada for {word}")
        jsonschema.validate(actual, analysis_schema)
        search = cpp(word, "search")
        if search["status"] != "unknown":
            raise AssertionError(f"{word} should be unknown")
        jsonschema.validate(search, search_schema)

    for word in SYNCOPE_FIXTURES:
        expected = load_json(["bin/words_json", word], cwd=ada_root)
        actual = cpp(word, "analysis")
        jsonschema.validate(actual, analysis_schema)
        if len(actual["analyses"]) != len(expected["analyses"]):
            raise AssertionError(f"C++ syncope count differs from Ada for {word}")

        derivations = [item["derivation"] for item in actual["analyses"]]
        if any(item["method"] != "syncope" or not item["steps"]
               for item in derivations):
            raise AssertionError(f"C++ did not retain syncope provenance for {word}")

        # The historical Ada JSON exporter reports the recovered lexical form
        # as regular. The native envelope intentionally retains why that form
        # was reached; every other canonical field must remain byte-equivalent.
        for actual_item, expected_item in zip(
                actual["analyses"], expected["analyses"], strict=True):
            actual_item["derivation"] = expected_item["derivation"]
        if actual != expected:
            raise AssertionError(f"C++ syncope semantics differ from Ada for {word}")

        search = cpp(word, "search")
        if any("rewriteIds" not in hit for hit in search["hits"]):
            raise AssertionError(f"C++ search lost syncope identity for {word}")
        jsonschema.validate(search, search_schema)

    for word in ORTHOGRAPHIC_FIXTURES:
        expected = load_json(["bin/words_json", word], cwd=ada_root)
        actual = cpp(word, "analysis")
        jsonschema.validate(actual, analysis_schema)
        if len(actual["analyses"]) != len(expected["analyses"]):
            raise AssertionError(
                f"C++ orthographic count differs from Ada for {word}")
        if any(item["derivation"]["method"] != "orthographic"
               for item in actual["analyses"]):
            raise AssertionError(
                f"C++ lost orthographic provenance for {word}")

        # Xxx markers are discarded by the historical canonical exporter in
        # the same way as Yyy syncope markers. Preserve the richer native path
        # while requiring every recovered lexical field to remain equivalent.
        for actual_item, expected_item in zip(
                actual["analyses"], expected["analyses"], strict=True):
            actual_item["derivation"] = expected_item["derivation"]
        if actual != expected:
            raise AssertionError(
                f"C++ orthographic semantics differ from Ada for {word}")

        search = cpp(word, "search")
        if any("rewriteIds" not in hit for hit in search["hits"]):
            raise AssertionError(
                f"C++ search lost orthographic identity for {word}")
        jsonschema.validate(search, search_schema)

    for phrase in COMPOUND_FIXTURES:
        expected = load_json(["bin/words_json", phrase], cwd=ada_root)
        actual = cpp(phrase, "analysis")
        jsonschema.validate(actual, analysis_schema)
        if len(actual["analyses"]) != len(expected["analyses"]):
            raise AssertionError(
                f"C++ compound count differs from Ada for {phrase}")

        # The historical exporter receives the full query as text but exposes
        # only the first token as normalized and loses the Ppp provenance.
        # The native contract intentionally fixes both presentation losses.
        expected["query"]["normalized"] = actual["query"]["normalized"]
        compound_count = 0
        for actual_item, expected_item in zip(
                actual["analyses"], expected["analyses"], strict=True):
            if actual_item["derivation"]["method"] == "compound":
                compound_count += 1
                expected_item["derivation"] = actual_item["derivation"]
        if compound_count == 0 or actual != expected:
            raise AssertionError(
                f"C++ compound semantics differ from Ada for {phrase}")

        search = cpp(phrase, "search")
        if sum("compound" in hit for hit in search["hits"]) != compound_count:
            raise AssertionError(
                f"C++ search lost compound identity for {phrase}")
        jsonschema.validate(search, search_schema)

    for word, expected_segments in TWO_WORD_FIXTURES.items():
        expected = load_json(
            ["bin/words_json", "--two-words=legacy", word], cwd=ada_root)
        actual = cpp(word, "analysis", two_words=True)
        jsonschema.validate(actual, analysis_schema)
        if actual["status"] != "unknown" or actual["analyses"]:
            raise AssertionError(
                f"{word} split must remain a suggestion, not an analysis")
        if len(actual.get("suggestions", [])) != 1:
            raise AssertionError(f"C++ lost Two_Words suggestion for {word}")

        suggestion = actual["suggestions"][0]
        segment_texts = tuple(
            segment["text"] for segment in suggestion["segments"])
        if segment_texts != expected_segments:
            raise AssertionError(
                f"C++ split boundary differs from Ada for {word}")

        # Ada flattens the two groups and reports them as a successful parse.
        # The native contract preserves their lexical content but groups the
        # low-confidence hypothesis and leaves the query unknown.
        actual_analyses = [
            analysis
            for segment in suggestion["segments"]
            for analysis in segment["analyses"]
        ]
        stable = lambda item: json.dumps(
            item, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        if sorted(actual_analyses, key=stable) != sorted(
                expected["analyses"], key=stable):
            raise AssertionError(
                f"C++ Two_Words lexical content differs from Ada for {word}")

        search = cpp(word, "search", two_words=True)
        if search["status"] != "unknown" or search["hits"]:
            raise AssertionError(
                f"search promoted speculative split for {word}")
        if len(search.get("suggestions", [])) != 1:
            raise AssertionError(
                f"search lost Two_Words grouping for {word}")
        jsonschema.validate(search, search_schema)

    blocked = cpp("insed", "analysis", two_words=True)
    expected_blocked = load_json(
        ["bin/words_json", "--two-words=legacy", "insed"], cwd=ada_root)
    if blocked != expected_blocked or "suggestions" in blocked:
        raise AssertionError("common-prefix guard differs from Ada")
    jsonschema.validate(blocked, analysis_schema)

    marked = cpp("puella\N{COMBINING MACRON}", "analysis")
    if marked["query"]["normalized"] != "puellā":
        raise AssertionError("NFD input was not normalized to NFC")
    if any(item["form"]["ending"] != "ā" for item in marked["analyses"]):
        raise AssertionError("surface ending did not preserve the macron")
    jsonschema.validate(marked, analysis_schema)


if __name__ == "__main__":
    main()
