#!/usr/bin/env python3
"""Regression checks for the canonical JSON exporter."""

from __future__ import annotations

import collections
import copy
import json
import os
import pathlib
import shutil
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
EXECUTABLE = ROOT / "bin" / "words_json"
SCHEMA = ROOT.parent / "schemas" / "analysis-v1.schema.json"
SEARCH_SCHEMA = ROOT.parent / "schemas" / "search-v1.schema.json"


def analyze(word: str, cwd: pathlib.Path = ROOT) -> dict[str, object]:
    environment = os.environ.copy()
    environment["WHITAKERS_WORDS_DATADIR"] = str(ROOT)
    result = subprocess.run(
        [str(EXECUTABLE), word],
        cwd=cwd,
        env=environment,
        check=True,
        capture_output=True,
        text=True,
    )
    document = json.loads(result.stdout)
    assert document["schema"] == "whitakers-words.analysis"
    assert document["schemaVersion"] == 1
    assert document["query"]["text"] == word
    return document


def validate_if_available(document: dict[str, object]) -> None:
    try:
        import jsonschema
    except ModuleNotFoundError:
        return
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(document)


def assert_schema_rejects_if_available(document: dict[str, object]) -> None:
    try:
        import jsonschema
    except ModuleNotFoundError:
        return
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    assert not jsonschema.Draft202012Validator(schema).is_valid(document)


def validate_search_schema_contract() -> None:
    schema = json.loads(SEARCH_SCHEMA.read_text(encoding="utf-8"))
    try:
        import jsonschema
    except ModuleNotFoundError:
        return

    validator = jsonschema.Draft202012Validator(schema)
    document = {
        "schema": "whitakers-words.search",
        "schemaVersion": 1,
        "datasetId": (
            "sha256:"
            "0123456789abcdef0123456789abcdef"
            "0123456789abcdef0123456789abcdef"
        ),
        "query": {"text": "rosae", "normalized": "rosae", "mode": "latin"},
        "status": "analyzed",
        "hits": [
            {"lexemeId": 33805, "ruleId": 1727, "addonIds": [], "scoreFlags": 0}
        ],
        "diagnostics": [],
    }
    validator.validate(document)

    invalid_unknown = copy.deepcopy(document)
    invalid_unknown["status"] = "unknown"
    assert not validator.is_valid(invalid_unknown)

    invalid_flags = copy.deepcopy(document)
    invalid_flags["hits"][0]["scoreFlags"] = 1
    assert not validator.is_valid(invalid_flags)


def main() -> None:
    validate_search_schema_contract()

    rosae = analyze("rosae")
    validate_if_available(rosae)
    counts = collections.Counter(
        analysis["partOfSpeech"] for analysis in rosae["analyses"]
    )
    assert counts == {"noun": 5, "participle": 4}
    assert {
        analysis["lexeme"]["dictionaryForm"]
        for analysis in rosae["analyses"]
    } == {"rosa, rosae", "rodo, rodere, rosi, rosus"}

    invalid_stem_key = copy.deepcopy(rosae)
    invalid_stem_key["analyses"][0]["form"]["stemKey"] = 9
    assert_schema_rejects_if_available(invalid_stem_key)

    invalid_noun_properties = copy.deepcopy(rosae)
    noun = next(
        analysis
        for analysis in invalid_noun_properties["analyses"]
        if analysis["lexeme"]["partOfSpeech"] == "noun"
    )
    noun["lexeme"]["properties"] = {"verbKind": None}
    assert_schema_rejects_if_available(invalid_noun_properties)

    with tempfile.TemporaryDirectory(prefix="words-json-profile-") as directory:
        alternate_cwd = pathlib.Path(directory)
        shutil.copy(
            ROOT / "test" / "WORD.MDV_template", alternate_cwd / "WORD.MDV"
        )
        (alternate_cwd / "WORD.MOD").write_text(
            "this configuration must be ignored\n", encoding="ascii"
        )
        assert analyze("rosae", alternate_cwd) == rosae
        assert not any(alternate_cwd.glob("WORD.STA"))
        assert not any(alternate_cwd.glob("WORD.OUT"))
        assert not any(alternate_cwd.glob("WORD.UNK"))

    unknown = analyze("zzzzzz")
    validate_if_available(unknown)
    assert unknown["status"] == "unknown"
    assert unknown["analyses"] == []

    qvae = analyze("QVAE")
    validate_if_available(qvae)
    assert qvae["query"] == {
        "text": "QVAE",
        "normalized": "quae",
        "mode": "latin",
    }

    ludica = analyze("ludica")
    validate_if_available(ludica)
    suffixes = {
        step["text"]
        for analysis in ludica["analyses"]
        for step in analysis["derivation"]["steps"]
        if step["type"] == "suffix"
    }
    assert suffixes == {"c", "ic"}

    anaticulus = analyze("anaticulus")
    validate_if_available(anaticulus)
    assert len(anaticulus["analyses"]) == 2
    assert all(
        analysis["derivation"]["method"] == "derived"
        and [step["text"] for step in analysis["derivation"]["steps"]]
        == ["icul"]
        for analysis in anaticulus["analyses"]
    )

    # The legacy engine applies one suffix rule per attempt. The doubled form
    # succeeds because "anaticul" is itself a lexical stem; the tripled form
    # would require Apply_Suffix to invoke itself and therefore stays unknown.
    anaticuliculus = analyze("anaticuliculus")
    validate_if_available(anaticuliculus)
    assert len(anaticuliculus["analyses"]) == 1
    assert anaticuliculus["analyses"][0]["lexeme"]["dictionaryForm"].startswith(
        "anaticula"
    )
    assert [
        step["text"]
        for step in anaticuliculus["analyses"][0]["derivation"]["steps"]
    ] == ["icul"]

    anaticuliculiculus = analyze("anaticuliculiculus")
    validate_if_available(anaticuliculiculus)
    assert anaticuliculiculus["status"] == "unknown"
    assert anaticuliculiculus["analyses"] == []

    part_of_speech_examples = {
        "bonus": "adjective",
        "amo": "verb",
        "bene": "adverb",
        "in": "preposition",
        "et": "conjunction",
        "heu": "interjection",
        "hic": "pronoun",
        "unus": "numeral",
        "amatum": "supine",
    }
    for word, expected in part_of_speech_examples.items():
        document = analyze(word)
        validate_if_available(document)
        assert expected in {
            analysis["partOfSpeech"] for analysis in document["analyses"]
        }

    roman = analyze("IIII")
    validate_if_available(roman)
    assert any(
        analysis["derivation"]["method"] == "roman-numeral"
        for analysis in roman["analyses"]
    )

    print("canonical JSON tests: PASS")


if __name__ == "__main__":
    main()
