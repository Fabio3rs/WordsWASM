#!/usr/bin/env python3

"""Validate editorial lexeme decisions against the exact generated review queue."""

from __future__ import annotations

import argparse
import json
import sys
import unicodedata
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import jsonschema


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIRECTORY.parents[2]
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

import suggest_quantity_evidence as quantity


REVIEW_SCHEMA_PATH = PROJECT_ROOT / "schemas/lexeme-review-candidate-v1.schema.json"
DECISION_SCHEMA_PATH = (
    PROJECT_ROOT / "schemas/lexeme-editorial-decision-v1.schema.json"
)
PART_NAMES = {
    "NOUN": "noun",
    "PRON": "pronoun",
    "ADJ": "adjective",
    "NUM": "numeral",
    "ADV": "adverb",
    "VERB": "verb",
    "PREP": "preposition",
    "CONJ": "conjunction",
    "INTERJ": "interjection",
}
GENDER_NAMES = {
    "m": "masculine",
    "f": "feminine",
    "n": "neuter",
    "c": "common",
}
RESOLVING_DISPOSITIONS = frozenset(
    {"accept_new", "merge_existing", "variant_of", "reject"}
)


class LedgerError(ValueError):
    """The queue or ledger cannot be interpreted as JSONL input."""


@dataclass(frozen=True)
class JsonlRecord:
    line: int
    value: dict[str, Any]


@dataclass(frozen=True)
class Issue:
    code: str
    line: int
    message: str
    decision_id: str | None = None
    draft_id: str | None = None
    path: str | None = None

    def record(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "code": self.code,
            "line": self.line,
            "message": self.message,
        }
        if self.decision_id is not None:
            result["decision_id"] = self.decision_id
        if self.draft_id is not None:
            result["draft_id"] = self.draft_id
        if self.path is not None:
            result["path"] = self.path
        return result


def load_schema(path: Path) -> dict[str, Any]:
    try:
        schema = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LedgerError(f"cannot read schema {path}: {error}") from error
    jsonschema.Draft202012Validator.check_schema(schema)
    return schema


def read_jsonl(path: Path) -> tuple[JsonlRecord, ...]:
    records: list[JsonlRecord] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise LedgerError(f"cannot read {path}: {error}") from error
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise LedgerError(f"{path}:{line_number}: invalid JSON") from error
        if not isinstance(value, dict):
            raise LedgerError(f"{path}:{line_number}: record must be an object")
        records.append(JsonlRecord(line_number, value))
    return tuple(records)


def json_path(error: jsonschema.ValidationError) -> str:
    return "$" + "".join(
        f"[{item}]" if isinstance(item, int) else f".{item}"
        for item in error.absolute_path
    )


def read_candidates(
    path: Path,
    validator: jsonschema.Draft202012Validator,
) -> dict[str, dict[str, Any]]:
    candidates: dict[str, dict[str, Any]] = {}
    for record in read_jsonl(path):
        errors = sorted(
            validator.iter_errors(record.value),
            key=lambda error: (list(error.absolute_path), error.message),
        )
        if errors:
            first = errors[0]
            raise LedgerError(
                f"{path}:{record.line}:{json_path(first)}: {first.message}"
            )
        draft_id = record.value["draft_id"]
        if draft_id in candidates:
            raise LedgerError(f"{path}:{record.line}: duplicate draft_id {draft_id!r}")
        candidates[draft_id] = record.value
    return candidates


def read_quantity_evidence_ids(path: Path | None) -> frozenset[str] | None:
    if path is None:
        return None
    identifiers: set[str] = set()
    for record in read_jsonl(path):
        if record.value.get("record") != "evidence":
            continue
        identifier = record.value.get("id")
        if not isinstance(identifier, str) or not identifier:
            raise LedgerError(f"{path}:{record.line}: evidence lacks a string id")
        if identifier in identifiers:
            raise LedgerError(f"{path}:{record.line}: duplicate evidence id {identifier!r}")
        identifiers.add(identifier)
    return frozenset(identifiers)


def decision_identity(record: JsonlRecord) -> tuple[str | None, str | None]:
    decision_id = record.value.get("decision_id")
    target = record.value.get("target")
    draft_id = target.get("draft_id") if isinstance(target, dict) else None
    return (
        decision_id if isinstance(decision_id, str) else None,
        draft_id if isinstance(draft_id, str) else None,
    )


def issue(
    code: str,
    record: JsonlRecord,
    message: str,
    *,
    path: str | None = None,
) -> Issue:
    decision_id, draft_id = decision_identity(record)
    return Issue(code, record.line, message, decision_id, draft_id, path)


def source_references(candidate: dict[str, Any]) -> frozenset[str]:
    heads = candidate["evidence"]["semantics"]["headword_evidence"]
    return frozenset(item["source_ref"] for item in heads)


def validate_canonical_structure(
    record: JsonlRecord, candidate: dict[str, Any]
) -> list[Issue]:
    problems: list[Issue] = []
    canonical = record.value["canonical"]
    lexical = candidate["structure"]["lexical"]
    part = lexical["part_of_speech"]
    expected_part = PART_NAMES[part]
    if canonical["part_of_speech"] != expected_part:
        problems.append(
            issue(
                "canonical_part_of_speech_mismatch",
                record,
                f"canonical POS must be {expected_part!r}",
                path="$.canonical.part_of_speech",
            )
        )

    expected_stems = {
        item["slot"]: item["stem"] for item in lexical["stems"]
    }
    actual_stems = {item["slot"]: item["ascii"] for item in canonical["stems"]}
    if actual_stems != expected_stems:
        problems.append(
            issue(
                "canonical_stems_mismatch",
                record,
                "canonical stems must exactly match the reviewed structure",
                path="$.canonical.stems",
            )
        )

    dictionary_form = canonical["dictionary_form"]
    if unicodedata.normalize("NFC", dictionary_form) != dictionary_form:
        problems.append(
            issue(
                "canonical_dictionary_form_not_nfc",
                record,
                "dictionary_form must be NFC",
                path="$.canonical.dictionary_form",
            )
        )
    word = quantity.extract_first_word(dictionary_form, require_quantity=False)
    if word is None or word.source != candidate["key"]["ascii_lemma"]:
        problems.append(
            issue(
                "canonical_dictionary_form_mismatch",
                record,
                "first dictionary-form word must normalize to the draft ASCII lemma",
                path="$.canonical.dictionary_form",
            )
        )
    elif word.proper != candidate["key"]["proper"]:
        problems.append(
            issue(
                "canonical_proper_mismatch",
                record,
                "dictionary-form capitalization differs from the reviewed key",
                path="$.canonical.dictionary_form",
            )
        )

    meaning = canonical["meaning"]
    if not meaning.strip():
        problems.append(
            issue(
                "empty_canonical_meaning",
                record,
                "canonical meaning must contain non-whitespace text",
                path="$.canonical.meaning",
            )
        )
    elif unicodedata.normalize("NFC", meaning) != meaning:
        problems.append(
            issue(
                "canonical_meaning_not_nfc",
                record,
                "canonical meaning must be NFC",
                path="$.canonical.meaning",
            )
        )

    properties = canonical["properties"]
    paradigm = lexical["paradigm"]
    paradigm_number = paradigm["declension_or_conjugation"]
    variant = paradigm["variant"]
    if part in {"NOUN", "PRON", "ADJ", "NUM"}:
        expected_properties: dict[str, Any] = {
            "declension": paradigm_number,
            "variant": variant,
        }
    elif part == "VERB":
        expected_properties = {"conjugation": paradigm_number, "variant": variant}
    else:
        expected_properties = {}
    if part == "NOUN" and "gender" in lexical:
        expected_properties["gender"] = GENDER_NAMES[lexical["gender"]]
    if part in {"ADJ", "ADV"}:
        expected_properties["degree"] = "positive"
    for name, expected in expected_properties.items():
        if properties.get(name) != expected:
            problems.append(
                issue(
                    "canonical_property_mismatch",
                    record,
                    f"canonical property {name!r} must equal {expected!r}",
                    path=f"$.canonical.properties.{name}",
                )
            )
    return problems


def validate_field_evidence(
    record: JsonlRecord, candidate: dict[str, Any]
) -> list[Issue]:
    field_evidence = record.value["field_evidence"]
    problems: list[Issue] = []
    for required in ("dictionary_form", "meaning", "properties"):
        if required not in field_evidence:
            problems.append(
                issue(
                    "missing_field_evidence",
                    record,
                    f"accept_new requires evidence for {required!r}",
                    path=f"$.field_evidence.{required}",
                )
            )
    known = source_references(candidate)
    for field, references in field_evidence.items():
        for reference in references:
            if reference not in known:
                problems.append(
                    issue(
                        "unknown_field_evidence",
                        record,
                        f"{field!r} references unknown source {reference!r}",
                        path=f"$.field_evidence.{field}",
                    )
                )
    return problems


def validate_quantity_references(
    record: JsonlRecord, evidence_ids: frozenset[str] | None
) -> list[Issue]:
    identifiers = record.value.get("quantity_evidence_ids", [])
    if not identifiers:
        return []
    if evidence_ids is None:
        return [
            issue(
                "quantity_manifest_required",
                record,
                "quantity_evidence_ids require --quantity-evidence",
                path="$.quantity_evidence_ids",
            )
        ]
    return [
        issue(
            "unknown_quantity_evidence",
            record,
            f"unknown quantity evidence id {identifier!r}",
            path="$.quantity_evidence_ids",
        )
        for identifier in identifiers
        if identifier not in evidence_ids
    ]


def validate_merge_target(
    record: JsonlRecord, candidate: dict[str, Any]
) -> list[Issue]:
    if record.value["disposition"] != "merge_existing" or "baseline" not in candidate:
        return []
    target = record.value["existing_lexeme"]
    found = any(
        item["dictionary"] == target["dictionary"]
        and item["entry_id"] == target["entry_id"]
        for item in candidate["baseline"]["lexemes"]
    )
    if found:
        return []
    return [
        issue(
            "merge_target_not_in_baseline",
            record,
            "merge_existing target is absent from the pinned engine baseline",
            path="$.existing_lexeme",
        )
    ]


def structural_signature(record: JsonlRecord) -> bytes:
    canonical = record.value["canonical"]
    projection = {
        "part_of_speech": canonical["part_of_speech"],
        "properties": canonical["properties"],
        "stems": sorted(canonical["stems"], key=lambda item: item["slot"]),
    }
    return json.dumps(
        projection, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def validate(
    candidates: dict[str, dict[str, Any]],
    decisions: Iterable[JsonlRecord],
    decision_validator: jsonschema.Draft202012Validator,
    quantity_evidence_ids: frozenset[str] | None = None,
) -> dict[str, Any]:
    records = tuple(decisions)
    problems: list[Issue] = []
    schema_valid: set[int] = set()
    dispositions: Counter[str] = Counter()

    for record_index, record in enumerate(records):
        errors = sorted(
            decision_validator.iter_errors(record.value),
            key=lambda error: (list(error.absolute_path), error.message),
        )
        if errors:
            for error in errors:
                problems.append(
                    issue(
                        "schema_violation",
                        record,
                        error.message,
                        path=json_path(error),
                    )
                )
            continue
        schema_valid.add(record_index)
        dispositions[record.value["disposition"]] += 1

    ids: dict[str, list[int]] = defaultdict(list)
    senses: dict[tuple[str, str], list[int]] = defaultdict(list)
    accepted_signatures: dict[bytes, list[int]] = defaultdict(list)
    structurally_checked: set[int] = set()
    for record_index in sorted(schema_valid):
        record = records[record_index]
        value = record.value
        decision_id = value["decision_id"]
        target = value["target"]
        draft_id = target["draft_id"]
        ids[decision_id].append(record_index)
        candidate = candidates.get(draft_id)
        if candidate is None:
            problems.append(
                issue(
                    "unknown_draft",
                    record,
                    f"decision targets unknown draft {draft_id!r}",
                    path="$.target.draft_id",
                )
            )
            continue
        if target["revision"] != candidate["revision"]:
            problems.append(
                issue(
                    "stale_revision",
                    record,
                    "decision revision differs from the current review candidate",
                    path="$.target.revision",
                )
            )
            continue

        for sense_id in target["sense_ids"]:
            senses[(draft_id, sense_id)].append(record_index)
        disposition = value["disposition"]
        if disposition == "accept_new":
            problems.extend(validate_canonical_structure(record, candidate))
            problems.extend(validate_field_evidence(record, candidate))
            problems.extend(
                validate_quantity_references(record, quantity_evidence_ids)
            )
            accepted_signatures[structural_signature(record)].append(record_index)
        elif disposition == "merge_existing":
            problems.extend(validate_merge_target(record, candidate))
        structurally_checked.add(record_index)

    for decision_id, indexes in sorted(ids.items()):
        if len(indexes) < 2:
            continue
        for record_index in indexes:
            problems.append(
                issue(
                    "duplicate_decision_id",
                    records[record_index],
                    f"decision_id {decision_id!r} occurs {len(indexes)} times",
                    path="$.decision_id",
                )
            )

    for (draft_id, sense_id), indexes in sorted(senses.items()):
        if len(indexes) < 2:
            continue
        for record_index in indexes:
            problems.append(
                issue(
                    "overlapping_sense_decision",
                    records[record_index],
                    f"sense {sense_id!r} of {draft_id!r} has multiple current decisions",
                    path="$.target.sense_ids",
                )
            )

    for indexes in accepted_signatures.values():
        if len(indexes) < 2:
            continue
        for record_index in indexes:
            problems.append(
                issue(
                    "accepted_structure_collision",
                    records[record_index],
                    "multiple accept_new decisions have the same structural signature",
                    path="$.canonical",
                )
            )

    problems.sort(
        key=lambda item: (
            item.line,
            item.code,
            item.path or "",
            item.message,
        )
    )
    invalid_lines = {item.line for item in problems}
    valid_indexes = {
        index
        for index in structurally_checked
        if records[index].line not in invalid_lines
    }
    decided_drafts = {
        records[index].value["target"]["draft_id"] for index in valid_indexes
    }
    resolved_drafts = {
        records[index].value["target"]["draft_id"]
        for index in valid_indexes
        if records[index].value["disposition"] in RESOLVING_DISPOSITIONS
    }
    unreviewed = sorted(set(candidates) - decided_drafts)
    unresolved = sorted(set(candidates) - resolved_drafts)
    return {
        "schema": "whitakers-words.lexeme-decision-validation-report.v1",
        "status": "valid" if not problems else "invalid",
        "candidates": len(candidates),
        "decisions": len(records),
        "valid_decisions": len(valid_indexes),
        "invalid_decisions": len(invalid_lines),
        "dispositions": dict(sorted(dispositions.items())),
        "decided_candidates": len(decided_drafts),
        "resolved_candidates": len(resolved_drafts),
        "unreviewed_candidates": unreviewed,
        "unresolved_candidates": unresolved,
        "stale_decisions": sum(item.code == "stale_revision" for item in problems),
        "errors": [item.record() for item in problems],
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidates", type=Path, help="generated review queue JSONL")
    parser.add_argument("decisions", type=Path, help="versioned editorial ledger JSONL")
    parser.add_argument("--report", type=Path, required=True, help="validation report JSON")
    parser.add_argument(
        "--quantity-evidence",
        type=Path,
        help="quantity evidence JSONL, required when decisions reference its IDs",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    review_validator = jsonschema.Draft202012Validator(
        load_schema(REVIEW_SCHEMA_PATH), format_checker=jsonschema.FormatChecker()
    )
    decision_validator = jsonschema.Draft202012Validator(
        load_schema(DECISION_SCHEMA_PATH), format_checker=jsonschema.FormatChecker()
    )
    candidates = read_candidates(arguments.candidates, review_validator)
    decisions = read_jsonl(arguments.decisions)
    evidence_ids = read_quantity_evidence_ids(arguments.quantity_evidence)
    result = validate(candidates, decisions, decision_validator, evidence_ids)
    arguments.report.write_text(
        json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if result["status"] == "valid":
        print(
            f'validated {result["valid_decisions"]} decisions; '
            f'{len(result["unresolved_candidates"])} candidates remain unresolved'
        )
        return 0
    print(
        f'ledger invalid: {len(result["errors"])} error(s) in '
        f'{result["invalid_decisions"]} decision(s)',
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
