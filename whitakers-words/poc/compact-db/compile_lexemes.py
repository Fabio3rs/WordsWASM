#!/usr/bin/env python3

"""Compile validated accept_new decisions into packer-only LEXEMES.LAT JSONL."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any, Iterable

import jsonschema


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

import validate_lexeme_decisions as ledger


OUTPUT_SCHEMA = "whitakers-words.compiled-lexeme.v1"
PART_CODES = {
    "noun": 1,
    "pronoun": 2,
    "adjective": 4,
    "numeral": 5,
    "adverb": 6,
    "verb": 7,
    "preposition": 10,
    "conjunction": 11,
    "interjection": 12,
}
GENDERS = {"masculine": 1, "feminine": 2, "neuter": 3, "common": 4}
DEGREES = {"positive": 1, "comparative": 2, "superlative": 3}
NOUN_KINDS = {
    "singular-only": 1,
    "plural-only": 2,
    "abstract": 3,
    "group": 4,
    "proper-name": 5,
    "person": 6,
    "thing": 7,
    "locale": 8,
    "place": 9,
}
PRONOUN_KINDS = {
    "personal": 1,
    "relative": 2,
    "reflexive": 3,
    "demonstrative": 4,
    "interrogative": 5,
    "indefinite": 6,
    "adjectival": 7,
}
NUMERAL_TYPES = {"cardinal": 1, "ordinal": 2, "distributive": 3, "adverbial": 4}
VERB_KINDS = {
    "to-be": 1,
    "compound-of-to-be": 2,
    "governs-genitive": 3,
    "governs-dative": 4,
    "governs-ablative": 5,
    "transitive": 6,
    "intransitive": 7,
    "impersonal": 8,
    "deponent": 9,
    "semideponent": 10,
    "perfect-definite": 11,
}
CASES = {
    "nominative": 1,
    "vocative": 2,
    "genitive": 3,
    "locative": 4,
    "dative": 5,
    "ablative": 6,
    "accusative": 7,
}
AGES = {
    "archaic": 1,
    "early": 2,
    "classical": 3,
    "late": 4,
    "later": 5,
    "medieval": 6,
    "scholarly": 7,
    "modern": 8,
}
SUBJECTS = {
    "agriculture": 1,
    "biological-medical": 2,
    "drama-arts": 3,
    "ecclesiastic": 4,
    "grammar-literature": 5,
    "legal-government": 6,
    "poetic": 7,
    "science-philosophy": 8,
    "technical": 9,
    "military": 10,
    "mythology": 11,
}
GEOGRAPHIES = {
    "africa": 1,
    "britain": 2,
    "china": 3,
    "scandinavia": 4,
    "egypt": 5,
    "france-gaul": 6,
    "germany": 7,
    "greece": 8,
    "italy-rome": 9,
    "india": 10,
    "balkans": 11,
    "netherlands": 12,
    "persia": 13,
    "near-east": 14,
    "russia": 15,
    "spain-iberia": 16,
    "eastern-europe": 17,
}
FREQUENCIES = {
    "very-frequent": 1,
    "frequent": 2,
    "common": 3,
    "lesser": 4,
    "uncommon": 5,
    "very-rare": 6,
    "inscription": 7,
    "graffiti": 8,
    "pliny": 9,
}
OTHER_DICTIONARIES_SOURCE = 17


class CompileError(ValueError):
    """A valid editorial decision cannot be represented by the microformat."""


def enum_value(
    value: Any, values: dict[str, int], context: str, *, optional: bool = False
) -> int:
    if optional and value is None:
        return 0
    if not isinstance(value, str) or value not in values:
        expected = ", ".join(sorted(values))
        raise CompileError(f"{context}: expected one of {expected}")
    return values[value]


def exact_properties(properties: dict[str, Any], allowed: set[str], context: str) -> None:
    unexpected = sorted(set(properties) - allowed)
    if unexpected:
        raise CompileError(
            f"{context}: properties would be lost by the runtime: {', '.join(unexpected)}"
        )


def class_payload(canonical: dict[str, Any], context: str) -> tuple[int, int]:
    part = canonical["part_of_speech"]
    properties = canonical["properties"]
    if part == "noun":
        exact_properties(properties, {"declension", "variant", "gender", "nounKind"}, context)
        gender = enum_value(properties.get("gender"), GENDERS, f"{context}.gender")
        noun_kind = enum_value(
            properties.get("nounKind"), NOUN_KINDS, f"{context}.nounKind", optional=True
        )
        return gender | (noun_kind << 3), 0
    if part == "pronoun":
        exact_properties(properties, {"declension", "variant", "pronounKind"}, context)
        return (
            enum_value(
                properties.get("pronounKind"),
                PRONOUN_KINDS,
                f"{context}.pronounKind",
                optional=True,
            ),
            0,
        )
    if part == "adjective":
        exact_properties(properties, {"declension", "variant", "degree"}, context)
        return enum_value(properties.get("degree"), DEGREES, f"{context}.degree"), 0
    if part == "numeral":
        exact_properties(
            properties,
            {"declension", "variant", "numeralType", "numeralValue"},
            context,
        )
        numeral_type = enum_value(
            properties.get("numeralType"), NUMERAL_TYPES, f"{context}.numeralType"
        )
        numeral_value = properties.get("numeralValue")
        if not isinstance(numeral_value, int) or not 0 <= numeral_value <= 1000:
            raise CompileError(f"{context}.numeralValue: expected integer in 0..1000")
        return numeral_type | (numeral_value << 3), numeral_value
    if part == "adverb":
        exact_properties(properties, {"degree"}, context)
        return enum_value(properties.get("degree"), DEGREES, f"{context}.degree"), 0
    if part == "verb":
        exact_properties(properties, {"conjugation", "variant", "verbKind"}, context)
        return (
            enum_value(
                properties.get("verbKind"), VERB_KINDS, f"{context}.verbKind", optional=True
            ),
            0,
        )
    if part == "preposition":
        exact_properties(properties, {"governs"}, context)
        return enum_value(properties.get("governs"), CASES, f"{context}.governs"), 0
    exact_properties(properties, set(), context)
    return 0, 0


def paradigm(canonical: dict[str, Any], context: str) -> int:
    properties = canonical["properties"]
    part = canonical["part_of_speech"]
    if part in {"noun", "pronoun", "adjective", "numeral"}:
        which = properties.get("declension")
        variant = properties.get("variant")
    elif part == "verb":
        which = properties.get("conjugation")
        variant = properties.get("variant")
    else:
        return 0
    if not isinstance(which, int) or not 0 <= which <= 9:
        raise CompileError(f"{context}: paradigm number must be in 0..9")
    if not isinstance(variant, int) or not 0 <= variant <= 9:
        raise CompileError(f"{context}: paradigm variant must be in 0..9")
    return (which << 4) | variant


def translation(canonical: dict[str, Any], context: str) -> int:
    metadata = canonical["metadata"]
    if metadata["source"] != "other-dictionaries":
        raise CompileError(f"{context}.source: only other-dictionaries is importable")
    age = enum_value(metadata["age"], AGES, f"{context}.age", optional=True)
    subject = enum_value(
        metadata["subject"], SUBJECTS, f"{context}.subject", optional=True
    )
    geography = enum_value(
        metadata["geography"], GEOGRAPHIES, f"{context}.geography", optional=True
    )
    frequency = enum_value(
        metadata["frequency"], FREQUENCIES, f"{context}.frequency", optional=True
    )
    return (
        age
        | (subject << 4)
        | (geography << 8)
        | (frequency << 13)
        | (OTHER_DICTIONARIES_SOURCE << 17)
    )


def compile_decision(value: dict[str, Any]) -> dict[str, Any]:
    canonical = value["canonical"]
    context = value["decision_id"]
    meaning = canonical["meaning"]
    if len(meaning.encode("utf-8")) > 255:
        raise CompileError(f"{context}.canonical.meaning: exceeds 255 UTF-8 bytes")
    stems = ["", "", "", ""]
    for item in canonical["stems"]:
        stems[item["slot"] - 1] = item["ascii"]
    payload, numeric_value = class_payload(canonical, f"{context}.canonical.properties")
    return {
        "schema": OUTPUT_SCHEMA,
        "decision_id": value["decision_id"],
        "stems": stems,
        "meaning": meaning,
        "part_of_speech": PART_CODES[canonical["part_of_speech"]],
        "paradigm": paradigm(canonical, f"{context}.canonical.properties"),
        "class_payload": payload,
        "numeric_value": numeric_value,
        "translation": translation(canonical, f"{context}.canonical.metadata"),
    }


def compile_records(decisions: Iterable[ledger.JsonlRecord]) -> list[dict[str, Any]]:
    accepted = [
        compile_decision(record.value)
        for record in decisions
        if record.value.get("disposition") == "accept_new"
    ]
    # WHY: runtime IDs are positions. Sorting by semantic content prevents a
    # harmless ledger reorder from renumbering every imported lexeme.
    accepted.sort(
        key=lambda item: (
            tuple(item["stems"]),
            item["part_of_speech"],
            item["paradigm"],
            item["class_payload"],
            item["decision_id"],
        )
    )
    return accepted


def render_jsonl(records: Iterable[dict[str, Any]]) -> str:
    return "".join(
        json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
        for record in records
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidates", type=Path, help="generated review queue JSONL")
    parser.add_argument("decisions", type=Path, help="versioned editorial ledger JSONL")
    parser.add_argument("--output", type=Path, required=True, help="compiled LEXEMES.LAT")
    parser.add_argument("--report", type=Path, required=True, help="compilation report JSON")
    parser.add_argument("--quantity-evidence", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    review_validator = jsonschema.Draft202012Validator(
        ledger.load_schema(ledger.REVIEW_SCHEMA_PATH),
        format_checker=jsonschema.FormatChecker(),
    )
    decision_validator = jsonschema.Draft202012Validator(
        ledger.load_schema(ledger.DECISION_SCHEMA_PATH),
        format_checker=jsonschema.FormatChecker(),
    )
    candidates = ledger.read_candidates(arguments.candidates, review_validator)
    decisions = ledger.read_jsonl(arguments.decisions)
    evidence_ids = ledger.read_quantity_evidence_ids(arguments.quantity_evidence)
    validation = ledger.validate(candidates, decisions, decision_validator, evidence_ids)
    if validation["status"] != "valid":
        raise CompileError(
            f"editorial ledger is invalid ({len(validation['errors'])} error(s))"
        )
    records = compile_records(decisions)
    output = render_jsonl(records)
    digest = hashlib.sha256(output.encode("utf-8")).hexdigest()
    report = {
        "schema": "whitakers-words.lexeme-compilation-report.v1",
        "status": "compiled",
        "validated_decisions": validation["valid_decisions"],
        "accepted_lexemes": len(records),
        "unresolved_candidates": validation["unresolved_candidates"],
        "output_sha256": f"sha256:{digest}",
    }
    # WHY: invalid input must not replace the last known-good microdata. All
    # validation and compilation therefore finish before either output changes.
    arguments.output.write_text(output, encoding="utf-8")
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"compiled {len(records)} accepted lexeme(s) into {arguments.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CompileError, ledger.LedgerError, OSError) as error:
        print(f"compile_lexemes: {error}", file=sys.stderr)
        raise SystemExit(1) from error
