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
EQUIVALENT_FIXTURES = (
    NOUN_FIXTURES + ADJECTIVE_FIXTURES + DERIVED_FIXTURES + PREFIX_FIXTURES
    + TACKON_FIXTURES + PACKON_FIXTURES + SEMANTIC_FIXTURES
    + DERIVED_SEMANTIC_FIXTURES + UNIQUE_FIXTURES + ROMAN_FIXTURES
)


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


def load_json_lines(command: list[str], queries: tuple[str, ...],
                    cwd: Path | None = None) -> dict[str, dict]:
    if len(set(queries)) != len(queries):
        raise AssertionError("batch queries must be unique")
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        input="".join(f"{query}\n" for query in queries),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    documents = [json.loads(line) for line in completed.stdout.splitlines()]
    if len(documents) != len(queries):
        raise AssertionError(
            f"batch result count differs: queries={len(queries)}, "
            f"documents={len(documents)}")
    return dict(zip(queries, documents, strict=True))


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
    analysis_validator = jsonschema.Draft202012Validator(analysis_schema)
    search_validator = jsonschema.Draft202012Validator(search_schema)

    subprocess.run([
        sys.executable,
        str(ada_root / "poc/compact-db/import_ada_rewrites.py"),
        str(ada_root),
        "--check", str(ada_root / "REWRITES.LAT"),
    ], check=True)

    native_base = [
        str(args.cpp),
        "--database", str(database),
        "--dataset-id", DATASET_ID,
    ]

    def cpp_batch(output_format: str, queries: tuple[str, ...],
                  two_words: bool = False) -> dict[str, dict]:
        command = [
            *native_base,
            "--format", output_format,
        ]
        if two_words:
            command.append("--two-words=legacy")
        command.append("--batch-json-lines")
        return load_json_lines(command, queries)

    standard_queries = (
        EQUIVALENT_FIXTURES + UNKNOWN_FIXTURES + SYNCOPE_FIXTURES
        + ORTHOGRAPHIC_FIXTURES + COMPOUND_FIXTURES
    )
    marked_query = "puella\N{COMBINING MACRON}"
    two_word_queries = tuple(TWO_WORD_FIXTURES)
    cpp_documents = {
        **{
            (word, "analysis", False): document
            for word, document in cpp_batch(
                "analysis", standard_queries + (marked_query,)).items()
        },
        **{
            (word, "search", False): document
            for word, document in cpp_batch("search", standard_queries).items()
        },
        **{
            (word, "analysis", True): document
            for word, document in cpp_batch(
                "analysis", two_word_queries + ("insed",), True).items()
        },
        **{
            (word, "search", True): document
            for word, document in cpp_batch(
                "search", two_word_queries, True).items()
        },
    }

    def cpp(word: str, output_format: str,
            two_words: bool = False) -> dict:
        key = (word, output_format, two_words)
        if key not in cpp_documents:
            raise AssertionError(f"unbatched C++ query: {key}")
        return cpp_documents.pop(key)

    for word in EQUIVALENT_FIXTURES:
        expected = load_json(["bin/words_json", word], cwd=ada_root)
        actual = cpp(word, "analysis")
        if actual != expected:
            raise AssertionError(f"C++ differs from Ada for {word}")
        analysis_validator.validate(actual)
        search_validator.validate(cpp(word, "search"))

    for word in UNKNOWN_FIXTURES:
        expected = load_json(["bin/words_json", word], cwd=ada_root)
        actual = cpp(word, "analysis")
        if actual != expected:
            raise AssertionError(f"C++ differs from Ada for {word}")
        analysis_validator.validate(actual)
        search = cpp(word, "search")
        if search["status"] != "unknown":
            raise AssertionError(f"{word} should be unknown")
        search_validator.validate(search)

    for word in SYNCOPE_FIXTURES:
        expected = load_json(["bin/words_json", word], cwd=ada_root)
        actual = cpp(word, "analysis")
        analysis_validator.validate(actual)
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
        search_validator.validate(search)

    for word in ORTHOGRAPHIC_FIXTURES:
        expected = load_json(["bin/words_json", word], cwd=ada_root)
        actual = cpp(word, "analysis")
        analysis_validator.validate(actual)
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
        search_validator.validate(search)

    for phrase in COMPOUND_FIXTURES:
        expected = load_json(["bin/words_json", phrase], cwd=ada_root)
        actual = cpp(phrase, "analysis")
        analysis_validator.validate(actual)
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
        search_validator.validate(search)

    for word, expected_segments in TWO_WORD_FIXTURES.items():
        expected = load_json(
            ["bin/words_json", "--two-words=legacy", word], cwd=ada_root)
        actual = cpp(word, "analysis", two_words=True)
        analysis_validator.validate(actual)
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
        search_validator.validate(search)

    blocked = cpp("insed", "analysis", two_words=True)
    expected_blocked = load_json(
        ["bin/words_json", "--two-words=legacy", "insed"], cwd=ada_root)
    if blocked != expected_blocked or "suggestions" in blocked:
        raise AssertionError("common-prefix guard differs from Ada")
    analysis_validator.validate(blocked)

    marked = cpp(marked_query, "analysis")
    if marked["query"]["normalized"] != "puellā":
        raise AssertionError("NFD input was not normalized to NFC")
    if any(item["form"]["ending"] != "ā" for item in marked["analyses"]):
        raise AssertionError("surface ending did not preserve the macron")
    analysis_validator.validate(marked)
    if cpp_documents:
        raise AssertionError(
            f"unused batched C++ queries: {sorted(cpp_documents)}")


if __name__ == "__main__":
    main()
