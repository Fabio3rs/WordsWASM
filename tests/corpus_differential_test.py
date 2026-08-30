#!/usr/bin/env python3

import argparse
import collections
import copy
import json
import re
import subprocess
from pathlib import Path

import jsonschema


DATASET_ID = "sha256:" + "0" * 64
EXPECTED_CORPUS_FORMS = 2726

# WHY: these remaining differences are deliberate native semantics, not
# unreviewed compatibility debt. Typed syncope filters candidates individually;
# the suffix IR keeps the derivational -e in the stem and can retain multiple
# licensed source homographs; quocumque collapses duplicate Ada packon paths.
ACCEPTED_NATIVE_DIFFERENCES = {
    "amore": "native-subset",
    "corde": "overlap",
    "die": "overlap",
    "honore": "overlap",
    "improbe": "overlap",
    "implessemque": "native-superset",
    "more": "native-subset",
    "nate": "overlap",
    "noris": "native-subset",
    "oblite": "overlap",
    "paris": "native-subset",
    "perfide": "overlap",
    "quocumque": "native-subset",
    "sancte": "overlap",
}

# Every one of the 33 previously reviewed compatibility cases is now either
# resolved or promoted above as an intentional native behavior.
KNOWN_COMPATIBILITY_DIFFERENCES: dict[str, str] = {}


def corpus_words(path: Path) -> list[str]:
    # WHY: repeated verse forms add runtime but no differential surface. The
    # sorted set makes every failure reproducible and still covers the entire
    # vocabulary observed in the historical acceptance corpus.
    return sorted(set(re.findall(r"[A-Za-z]+", path.read_text().lower())))


def json_lines(command: list[str], queries: list[str], cwd: Path) -> list[dict]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        input="".join(f"{query}\n" for query in queries),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return [json.loads(line) for line in completed.stdout.splitlines()]


def validate(document: dict, validator: jsonschema.protocols.Validator,
             label: str) -> None:
    errors = sorted(validator.iter_errors(document), key=lambda item: list(item.path))
    if errors:
        raise AssertionError(f"{label}: {errors[0].message}")


def without_provenance(document: dict) -> dict:
    projected = copy.deepcopy(document)
    for item in projected["analyses"]:
        item["derivation"] = None
    return projected


def semantic_items(document: dict) -> set[str]:
    projected = copy.deepcopy(document["analyses"])
    for item in projected:
        item["derivation"] = None
        # Dictionary-form synthesis is presentation covered by focused tests;
        # corpus equivalence keys on the stable ID and all lexical/morphology
        # fields. A set also makes Ada's duplicate packon records immaterial.
        item["lexeme"]["dictionaryForm"] = None
    return {
        json.dumps(item, ensure_ascii=False, sort_keys=True,
                   separators=(",", ":"))
        for item in projected
    }


def semantic_relation(ada: dict, native: dict) -> str:
    ada_items = semantic_items(ada)
    native_items = semantic_items(native)
    if native_items < ada_items:
        return "native-subset"
    if native_items > ada_items:
        return "native-superset"
    if native_items == ada_items:
        return "same-set"
    if native_items & ada_items:
        return "overlap"
    return "disjoint"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cpp", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    cpp = args.cpp.resolve()
    ada_root = root / "whitakers-words"
    words = corpus_words(ada_root / "test/01_aeneid/input.txt")
    if len(words) != EXPECTED_CORPUS_FORMS:
        raise AssertionError(
            f"Aeneid vocabulary changed: expected {EXPECTED_CORPUS_FORMS}, "
            f"found {len(words)}")

    database = ada_root / "poc/compact-db/output/words-poc-dense.wwdb"
    search_database = (
        ada_root / "poc/compact-db/output/words-poc-search-only.wwdb"
    )
    analysis_schema = json.loads(
        (root / "schemas/analysis-v1.schema.json").read_text())
    search_schema = json.loads(
        (root / "schemas/search-v1.schema.json").read_text())
    analysis_validator = jsonschema.Draft202012Validator(analysis_schema)
    search_validator = jsonschema.Draft202012Validator(search_schema)

    ada_documents = json_lines(
        ["bin/words_json", "--batch-json-lines"], words, ada_root)
    native_base = [
        str(cpp),
        "--database", str(database),
        "--dataset-id", DATASET_ID,
    ]
    native_documents = json_lines(
        native_base + ["--format", "analysis", "--batch-json-lines"],
        words,
        root,
    )
    search_documents = json_lines(
        native_base + ["--format", "search", "--batch-json-lines"],
        words,
        root,
    )
    search_only_documents = json_lines(
        [
            str(cpp),
            "--database", str(search_database),
            "--dataset-id", DATASET_ID,
            "--format", "search",
            "--batch-json-lines",
        ],
        words,
        root,
    )
    counts = (
        len(ada_documents), len(native_documents), len(search_documents),
        len(search_only_documents),
    )
    if counts != (len(words),) * 4:
        raise AssertionError(
            f"batch result count differs: words={len(words)}, "
            f"Ada/native/full-search/search-only={counts}")

    stats = collections.Counter()
    reviewed_differences = {}
    full_bytes = 0
    search_bytes = 0
    for word, ada, native, search, search_only in zip(
            words, ada_documents, native_documents, search_documents,
            search_only_documents,
            strict=True):
        validate(ada, analysis_validator, f"Ada analysis for {word}")
        validate(native, analysis_validator, f"native analysis for {word}")
        validate(search, search_validator, f"native search for {word}")
        validate(
            search_only, search_validator,
            f"native search-only profile for {word}",
        )
        if search_only != search:
            raise AssertionError(
                f"search-only WWDB differs from full WWDB for {word}"
            )
        if any(document["query"]["text"] != word
               for document in (ada, native, search)):
            raise AssertionError(f"batch query order was lost at {word}")
        if (search["query"] != native["query"] or
                search["status"] != native["status"]):
            raise AssertionError(
                f"analysis/search envelope differs for {word}")

        full_bytes += len(json.dumps(native, separators=(",", ":")))
        search_bytes += len(json.dumps(search, separators=(",", ":")))
        if ada == native:
            stats["exact"] += 1
            continue
        if without_provenance(ada) == without_provenance(native):
            # Provenance is intentionally richer in the native contract and
            # may expose rewrites or correct a legacy addon classification.
            # Equality after removing only this field proves that no lexical
            # or morphological content was admitted by that tolerance.
            stats["provenance-only"] += 1
            continue

        relation = semantic_relation(ada, native)
        if relation == "same-set":
            stats["semantic-equivalent"] += 1
        else:
            reviewed_differences[word] = relation

    expected_reviewed = {
        **ACCEPTED_NATIVE_DIFFERENCES,
        **KNOWN_COMPATIBILITY_DIFFERENCES,
    }
    if reviewed_differences != expected_reviewed:
        missing = expected_reviewed.keys() - reviewed_differences.keys()
        new = reviewed_differences.keys() - expected_reviewed.keys()
        changed = {
            word: (expected_reviewed[word], reviewed_differences[word])
            for word in expected_reviewed.keys() & reviewed_differences.keys()
            if expected_reviewed[word] != reviewed_differences[word]
        }
        raise AssertionError(
            "Aeneid semantic differential changed; "
            f"missing={sorted(missing)}, new={sorted(new)}, changed={changed}")

    if search_bytes >= full_bytes:
        raise AssertionError(
            f"search projection is not compact: full={full_bytes}, "
            f"search={search_bytes}")
    stats["accepted-native"] = len(ACCEPTED_NATIVE_DIFFERENCES)
    stats["known-compatibility"] = len(KNOWN_COMPATIBILITY_DIFFERENCES)
    print(
        f"Aeneid corpus: {len(words)} forms; {dict(stats)}; "
        f"search/full bytes={search_bytes}/{full_bytes}")


if __name__ == "__main__":
    main()
