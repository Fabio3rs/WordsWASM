#!/usr/bin/env python3

"""Compile traceable vowel-quantity evidence into dense QUANTITIES.LAT rows."""

from __future__ import annotations

import argparse
import json
import re
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


CONFIDENCES = frozenset({"confirmed", "probable", "needs_review"})
SOURCE_METHODS = frozenset(
    {
        "derived_lexicon",
        "established_lexicographic",
        "reviewed_ocr",
        "grammar_reference",
        "legacy_migration",
        "morphological_lexicon",
    }
)
SOURCE_RELIABILITIES = frozenset({"high", "reviewed", "auxiliary"})
PROMOTED_CONFIDENCE = "confirmed"
MACRON = "\N{COMBINING MACRON}"
BREVE = "\N{COMBINING BREVE}"
ASCII_FORM = re.compile(r"[a-z]+")


class QuantityImportError(ValueError):
    """Input evidence violates the deterministic promotion policy."""


@dataclass(frozen=True)
class Source:
    source_id: str
    title: str
    artifact: str
    version: str
    method: str
    reliability: str
    family: str
    independent_quantity_authority: bool
    note: str


@dataclass(frozen=True)
class Target:
    kind: str
    target_id: int
    slot: int | None

    def sort_key(self) -> tuple[int, int, int]:
        return (0 if self.kind == "inflection" else 1, self.target_id, self.slot or 0)

    def report(self) -> dict[str, Any]:
        if self.kind == "inflection":
            return {"kind": self.kind, "rule_id": self.target_id}
        return {"kind": self.kind, "dictionary_entry": self.target_id, "slot": self.slot}


@dataclass(frozen=True)
class Evidence:
    evidence_id: str
    target: Target
    base: str
    marked: str
    known: int
    long_vowel: int
    source_id: str
    locator: str
    witness: str
    confidence: str
    label: str
    note: str


@dataclass(frozen=True)
class PromotedQuantity:
    target: Target
    base: str
    known: int
    long_vowel: int
    label: str
    evidence_ids: tuple[str, ...]


@dataclass(frozen=True)
class Compilation:
    microdata: str
    report: str
    promoted: tuple[PromotedQuantity, ...]


def require_string(record: dict[str, Any], field: str, context: str) -> str:
    value = record.get(field)
    if not isinstance(value, str) or not value:
        raise QuantityImportError(f"{context}: {field} must be a non-empty string")
    return value


def optional_string(record: dict[str, Any], field: str, context: str) -> str:
    value = record.get(field, "")
    if not isinstance(value, str):
        raise QuantityImportError(f"{context}: {field} must be a string")
    return value


def require_positive_int(record: dict[str, Any], field: str, context: str) -> int:
    value = record.get(field)
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise QuantityImportError(f"{context}: {field} must be a positive integer")
    return value


def require_nonnegative_int(record: dict[str, Any], field: str, context: str) -> int:
    value = record.get(field)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise QuantityImportError(f"{context}: {field} must be a nonnegative integer")
    return value


def require_bool(record: dict[str, Any], field: str, context: str) -> bool:
    value = record.get(field)
    if not isinstance(value, bool):
        raise QuantityImportError(f"{context}: {field} must be a boolean")
    return value


def parse_target(value: Any, context: str) -> Target:
    if not isinstance(value, dict):
        raise QuantityImportError(f"{context}: target must be an object")
    kind = require_string(value, "kind", context)
    if kind == "inflection":
        rule_id = require_nonnegative_int(value, "rule_id", context)
        if rule_id > 0xFFFF:
            raise QuantityImportError(f"{context}: inflection rule_id exceeds u16")
        return Target(kind, rule_id, None)
    if kind == "stem":
        entry = require_positive_int(value, "dictionary_entry", context)
        slot = require_positive_int(value, "slot", context)
        if entry > 0xFFFF:
            raise QuantityImportError(f"{context}: dictionary_entry exceeds u16")
        if slot > 4:
            raise QuantityImportError(f"{context}: stem slot must be in 1..4")
        return Target(kind, entry, slot)
    raise QuantityImportError(f"{context}: unsupported target kind {kind!r}")


def quantity_masks(base: str, marked: str, context: str) -> tuple[int, int]:
    if ASCII_FORM.fullmatch(base) is None:
        raise QuantityImportError(f"{context}: base must contain lowercase ASCII letters")

    # WHY: deriving masks from an actual marked witness keeps bit positions
    # reviewable and avoids hand-maintained decimal masks drifting from text.
    letters: list[str] = []
    marks: dict[int, str] = {}
    for character in unicodedata.normalize("NFD", marked):
        if unicodedata.combining(character) == 0:
            if character < "a" or character > "z":
                raise QuantityImportError(
                    f"{context}: marked form must decompose to lowercase ASCII letters"
                )
            letters.append(character)
            continue
        if character not in {MACRON, BREVE} or not letters:
            raise QuantityImportError(f"{context}: unsupported or detached combining mark")
        position = len(letters) - 1
        if position in marks:
            raise QuantityImportError(f"{context}: a letter has multiple quantity marks")
        if letters[position] not in "aeiouy":
            raise QuantityImportError(f"{context}: quantity mark is attached to a consonant")
        marks[position] = character

    if "".join(letters) != base:
        raise QuantityImportError(f"{context}: base and marked form have different letters")
    if not marks:
        raise QuantityImportError(f"{context}: evidence has no explicit quantity mark")

    known = 0
    long_vowel = 0
    for position, mark in marks.items():
        known |= 1 << position
        if mark == MACRON:
            long_vowel |= 1 << position
    return known, long_vowel


def parse_records(records: Iterable[dict[str, Any]]) -> tuple[dict[str, Source], list[Evidence]]:
    sources: dict[str, Source] = {}
    evidence: list[Evidence] = []
    evidence_ids: set[str] = set()

    for ordinal, record in enumerate(records, start=1):
        context = f"record {ordinal}"
        if not isinstance(record, dict):
            raise QuantityImportError(f"{context}: JSON value must be an object")
        kind = require_string(record, "record", context)
        if kind == "source":
            source_id = require_string(record, "id", context)
            if source_id in sources:
                raise QuantityImportError(f"{context}: duplicate source {source_id!r}")
            method = require_string(record, "method", context)
            reliability = require_string(record, "reliability", context)
            if method not in SOURCE_METHODS:
                raise QuantityImportError(f"{context}: unsupported source method {method!r}")
            if reliability not in SOURCE_RELIABILITIES:
                raise QuantityImportError(
                    f"{context}: unsupported source reliability {reliability!r}"
                )
            sources[source_id] = Source(
                source_id,
                require_string(record, "title", context),
                require_string(record, "artifact", context),
                require_string(record, "version", context),
                method,
                reliability,
                require_string(record, "family", context),
                require_bool(record, "independent_quantity_authority", context),
                optional_string(record, "note", context),
            )
            continue
        if kind != "evidence":
            raise QuantityImportError(f"{context}: unsupported record kind {kind!r}")

        evidence_id = require_string(record, "id", context)
        if evidence_id in evidence_ids:
            raise QuantityImportError(f"{context}: duplicate evidence {evidence_id!r}")
        evidence_ids.add(evidence_id)
        target = parse_target(record.get("target"), context)
        base = require_string(record, "base", context)
        marked = require_string(record, "marked", context)
        maximum_length = 7 if target.kind == "inflection" else 18
        if len(base) > maximum_length:
            raise QuantityImportError(
                f"{context}: base exceeds the {maximum_length}-letter WWDB mask"
            )
        known, long_vowel = quantity_masks(base, marked, context)
        confidence = require_string(record, "confidence", context)
        if confidence not in CONFIDENCES:
            raise QuantityImportError(f"{context}: unsupported confidence {confidence!r}")
        evidence.append(
            Evidence(
                evidence_id,
                target,
                base,
                marked,
                known,
                long_vowel,
                require_string(record, "source", context),
                require_string(record, "locator", context),
                require_string(record, "witness", context),
                confidence,
                require_string(record, "label", context),
                optional_string(record, "note", context),
            )
        )

    for item in evidence:
        if item.source_id not in sources:
            raise QuantityImportError(
                f"evidence {item.evidence_id!r}: unknown source {item.source_id!r}"
            )
    return sources, evidence


def promote(
    sources: dict[str, Source], evidence: Iterable[Evidence]
) -> tuple[PromotedQuantity, ...]:
    grouped: dict[Target, list[Evidence]] = {}
    for item in evidence:
        if item.confidence == PROMOTED_CONFIDENCE:
            # WHY: authority of the source and confidence in this individual
            # observation are distinct; legacy migration alone cannot publish.
            if sources[item.source_id].reliability == "auxiliary":
                raise QuantityImportError(
                    f"evidence {item.evidence_id!r}: auxiliary source cannot be confirmed"
                )
            grouped.setdefault(item.target, []).append(item)

    output: list[PromotedQuantity] = []
    for target in sorted(grouped, key=Target.sort_key):
        items = sorted(grouped[target], key=lambda item: item.evidence_id)
        bases = {item.base for item in items}
        labels = {item.label for item in items}
        if len(bases) != 1 or len(labels) != 1:
            raise QuantityImportError(
                f"target {target.report()}: confirmed evidence disagrees on base or label"
            )

        known = 0
        long_vowel = 0
        for item in items:
            conflict = known & item.known & (long_vowel ^ item.long_vowel)
            if conflict:
                raise QuantityImportError(
                    f"target {target.report()}: confirmed quantity conflict at mask {conflict}"
                )
            long_vowel = (long_vowel & ~item.known) | item.long_vowel
            known |= item.known
        output.append(
            PromotedQuantity(
                target,
                items[0].base,
                known,
                long_vowel,
                items[0].label,
                tuple(item.evidence_id for item in items),
            )
        )
    return tuple(output)


def render_microdata(promoted: Iterable[PromotedQuantity]) -> str:
    lines = [
        "-- Generated by poc/compact-db/import_quantities.py; do not edit.",
        "-- Only confirmed evidence is promoted. Masks use zero-based ASCII positions.",
        "-- STEM entry IDs/slots are one-based; INFLECTION IDs below are zero-based.",
        "",
    ]
    previous_kind = ""
    for item in promoted:
        if previous_kind and previous_kind != item.target.kind:
            lines.append("")
        previous_kind = item.target.kind
        lines.append(f"-- {item.label}; evidence: {', '.join(item.evidence_ids)}")
        if item.target.kind == "inflection":
            lines.append(
                f"INFLECTION {item.target.target_id} {item.known} {item.long_vowel}"
            )
        else:
            lines.append(
                f"STEM {item.target.target_id} {item.target.slot} "
                f"{item.known} {item.long_vowel}"
            )
    return "\n".join(lines) + "\n"


def render_report(
    sources: dict[str, Source], evidence: Iterable[Evidence], promoted: Iterable[PromotedQuantity]
) -> str:
    evidence_list = sorted(evidence, key=lambda item: item.evidence_id)
    promoted_list = tuple(promoted)
    promoted_ids = {item_id for item in promoted_list for item_id in item.evidence_ids}
    document = {
        "format": "whitakers-words.quantity-import-report-1",
        "policy": "confirmed-only-conflict-fatal-1",
        "counts": {
            "sources": len(sources),
            "evidence": len(evidence_list),
            "confirmed_evidence": len(promoted_ids),
            "deferred_evidence": len(evidence_list) - len(promoted_ids),
            "promoted_targets": len(promoted_list),
        },
        "sources": [
            {
                "id": item.source_id,
                "title": item.title,
                "artifact": item.artifact,
                "version": item.version,
                "method": item.method,
                "reliability": item.reliability,
                "family": item.family,
                "independent_quantity_authority": (
                    item.independent_quantity_authority
                ),
                "note": item.note or None,
            }
            for item in sorted(sources.values(), key=lambda item: item.source_id)
        ],
        "promoted": [
            {
                "target": item.target.report(),
                "base": item.base,
                "known_mask": item.known,
                "long_mask": item.long_vowel,
                "label": item.label,
                "evidence_ids": list(item.evidence_ids),
            }
            for item in promoted_list
        ],
        "evidence": [
            {
                "id": item.evidence_id,
                "target": item.target.report(),
                "source": item.source_id,
                "locator": item.locator,
                "witness": item.witness,
                "base": item.base,
                "marked": item.marked,
                "known_mask": item.known,
                "long_mask": item.long_vowel,
                "confidence": item.confidence,
                "promoted": item.evidence_id in promoted_ids,
                "note": item.note or None,
            }
            for item in evidence_list
        ],
    }
    return json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise QuantityImportError(f"{path}:{line_number}: {error.msg}") from error
        if not isinstance(value, dict):
            raise QuantityImportError(f"{path}:{line_number}: record must be an object")
        records.append(value)
    return records


def compile_evidence(path: Path) -> Compilation:
    sources, evidence = parse_records(load_jsonl(path))
    promoted = promote(sources, evidence)
    return Compilation(
        render_microdata(promoted),
        render_report(sources, evidence, promoted),
        promoted,
    )


def check_equal(expected_path: Path, actual: str, label: str) -> None:
    if expected_path.read_text(encoding="utf-8") != actual:
        raise QuantityImportError(f"{expected_path} differs from generated {label}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    destination = parser.add_mutually_exclusive_group()
    destination.add_argument("--output", type=Path, help="write generated QUANTITIES.LAT")
    destination.add_argument("--check", type=Path, help="verify an existing QUANTITIES.LAT")
    parser.add_argument("--check-report", type=Path, help="verify an existing JSON report")
    parser.add_argument("--report", type=Path, help="write the audit report")
    args = parser.parse_args()

    try:
        result = compile_evidence(args.evidence)
        if args.check is not None:
            check_equal(args.check, result.microdata, "microdata")
        elif args.output is not None:
            args.output.write_text(result.microdata, encoding="utf-8")
        else:
            print(result.microdata, end="")
        if args.check_report is not None:
            check_equal(args.check_report, result.report, "report")
        if args.report is not None:
            args.report.write_text(result.report, encoding="utf-8")
    except (OSError, QuantityImportError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
