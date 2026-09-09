#!/usr/bin/env python3

"""Build deliberately non-editorial appendix loads up to the WWDB u16 limit.

This is a capacity and cost probe.  It consumes the deterministic output of
``audit_lexical_expansion.py`` and never opens an external dictionary itself.
The resulting LEXEMES.LAT is packer input, not publication material.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from collections import Counter
from pathlib import Path
from typing import Any, Iterable, Iterator


COMPILED_SCHEMA = "whitakers-words.compiled-lexeme.v1"
REPORT_SCHEMA = "whitakers-words.u16-appendix-poc-report.v1"
SELECTION_SCHEMA = "whitakers-words.u16-appendix-poc-selection.v1"
DICTIONARY_RECORD_SIZE = 180
STEM_RECORD_SIZE = 56
U16_MAX = 65_535
U16_CARDINALITY = U16_MAX + 1
ASCII_STEM = re.compile(r"[a-z]{1,18}\Z")

PART_CODES = {
    "NOUN": 1,
    "PRON": 2,
    "ADJ": 4,
    "NUM": 5,
    "ADV": 6,
    "VERB": 7,
    "PREP": 10,
    "CONJ": 11,
    "INTERJ": 12,
}
INVARIABLE_PARTS = frozenset({"ADV", "CONJ", "INTERJ"})
# PREP needs a governed case.  The expansion audit does not normalize that
# field, so pretending that a bare headword is sufficient produced records
# which the engine could never match.  Leave prepositions to a later probe.
HEADWORD_ONLY_PARTS = frozenset({"NOUN", "ADJ"}) | INVARIABLE_PARTS
GENDERS = {"m": 1, "f": 2, "n": 3, "c": 4}
OTHER_DICTIONARIES_SOURCE = 17
UNCOMMON_FREQUENCY = 5
POC_TRANSLATION = (UNCOMMON_FREQUENCY << 13) | (OTHER_DICTIONARIES_SOURCE << 17)


class AppendixError(ValueError):
    """The requested capacity probe cannot be represented by the current WWDB."""


def read_jsonl(path: Path) -> Iterator[dict[str, Any]]:
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise AppendixError(f"{path}:{line_number}: invalid JSON") from error
            if not isinstance(value, dict):
                raise AppendixError(f"{path}:{line_number}: expected an object")
            yield value


def render_jsonl(records: Iterable[dict[str, Any]]) -> str:
    return "".join(
        json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
        for record in records
    )


def unique_data_lines(path: Path) -> list[str]:
    lines = [
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("--")
    ]
    if len(lines) % 3:
        raise AppendixError(f"{path}: incomplete UNIQUES.LAT record")
    return lines


def unique_surfaces(path: Path) -> set[str]:
    return set(unique_data_lines(path)[::3])


def count_uniques(path: Path) -> int:
    return len(unique_data_lines(path)) // 3


def fixed_ascii(record: bytes, offset: int, size: int) -> str:
    return record[offset : offset + size].decode("ascii").rstrip(" \x00")


def legacy_signatures(path: Path) -> tuple[set[tuple[Any, ...]], set[str]]:
    data = path.read_bytes()
    if len(data) % DICTIONARY_RECORD_SIZE:
        raise AppendixError(f"{path}: unexpected DICTFILE.GEN size")
    signatures: set[tuple[Any, ...]] = set()
    stem_strings: set[str] = set()
    paradigm_parts = {1, 2, 3, 4, 5, 7, 8, 9}
    for offset in range(0, len(data), DICTIONARY_RECORD_SIZE):
        record = data[offset : offset + DICTIONARY_RECORD_SIZE]
        stems = tuple(fixed_ascii(record, slot * 18, 18) for slot in range(4))
        stem_strings.update(stems)
        part = record[72]
        if part in paradigm_parts:
            which = struct.unpack_from("<I", record, 76)[0]
            variant = struct.unpack_from("<I", record, 80)[0]
            paradigm = (which << 4) | variant
        else:
            paradigm = 0
        attribute_0 = 0
        attribute_1 = 0
        numeric_value = 0
        if part == 1:
            attribute_0, attribute_1 = record[84], record[85]
        elif part in {2, 3, 4, 7}:
            attribute_0 = record[84]
        elif part == 5:
            attribute_0 = record[84]
            numeric_value = struct.unpack_from("<I", record, 88)[0]
        elif part in {6, 10}:
            attribute_0 = record[76]
        signatures.add(
            (stems, part, paradigm, attribute_0, attribute_1, numeric_value)
        )
    return signatures, stem_strings


def top_paradigms(report: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for mapping in report["morphology_crosswalk"]["mappings"]:
        observed = mapping.get("observed_paradigms", [])
        if observed:
            result[mapping["morphology_key"]] = max(
                observed,
                key=lambda item: (
                    int(item["witnesses"]),
                    -int(item["declension_or_conjugation"]),
                    -int(item["variant"]),
                ),
            )
    return result


def top_templates(report: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for mapping in report["stem_template_crosswalk"]["mappings"]:
        observed = mapping.get("observed_templates", [])
        if observed:
            result[mapping["morphology_key"]] = max(
                observed, key=lambda item: int(item["witnesses"])
            )
    return result


def candidate_priority(candidate: dict[str, Any]) -> tuple[Any, ...]:
    validation = candidate.get("latin_german_form_validation", {}).get("status")
    return (
        0 if candidate.get("support") == "corroborated_independent" else 1,
        0 if not candidate.get("proper") else 1,
        0 if validation == "all_slots_attested" else 1,
        -int(candidate.get("independent_family_count", 0)),
        -int(candidate.get("witness_count", 0)),
        candidate.get("ascii_lemma", ""),
        candidate.get("part_of_speech", ""),
    )


def class_payload(candidate: dict[str, Any]) -> int:
    part = candidate["part_of_speech"]
    if part == "NOUN":
        genders = candidate.get("genders", [])
        gender = GENDERS.get(genders[0], 0) if len(genders) == 1 else 0
        noun_kind = 5 if candidate.get("proper") else 0
        return gender | (noun_kind << 3)
    if part in {"ADJ", "ADV"}:
        return 1  # positive degree
    if part == "NUM":
        return 1  # cardinal, unknown numeric value represented as zero in this PoC
    return 0


def clean_meaning(candidate: dict[str, Any]) -> str:
    preference = {
        "faria": 0,
        "lewis": 1,
        "gaffiot": 2,
        "latin-german": 3,
        "collatinus-derived": 4,
    }
    witnesses = sorted(
        candidate.get("witnesses", []),
        key=lambda item: (
            preference.get(str(item.get("source_family")), 9),
            str(item.get("source_entry_id", "")),
        ),
    )
    raw = next(
        (str(item.get("head", "")) for item in witnesses if item.get("head")),
        str(candidate["ascii_lemma"]),
    )
    text = " ".join(raw.split())
    prefix = "[POC] "
    available = 255 - len(prefix.encode("utf-8"))
    encoded = text.encode("utf-8")[:available]
    while True:
        try:
            return prefix + encoded.decode("utf-8")
        except UnicodeDecodeError:
            encoded = encoded[:-1]


def signature(record: dict[str, Any]) -> tuple[Any, ...]:
    payload = int(record["class_payload"])
    part = int(record["part_of_speech"])
    attribute_0 = payload
    attribute_1 = 0
    if part == 1:
        attribute_0 = payload & 0x07
        attribute_1 = payload >> 3
    elif part == 5:
        attribute_0 = payload & 0x07
    return (
        tuple(record["stems"]),
        part,
        int(record["paradigm"]),
        attribute_0,
        attribute_1,
        int(record["numeric_value"]),
    )


def decision_id(profile: str, candidate: dict[str, Any], method: str) -> str:
    evidence = "|".join(
        sorted(
            f'{item.get("source")}:{item.get("source_entry_id")}'
            for item in candidate.get("witnesses", [])
        )
    )
    digest = hashlib.sha256(evidence.encode("utf-8")).hexdigest()[:12]
    return (
        f'poc-u16:{profile}:{method}:{candidate["part_of_speech"].lower()}:'
        f'{candidate["ascii_lemma"]}:{digest}'
    )


def compiled_record(
    profile: str,
    candidate: dict[str, Any],
    method: str,
    stems: list[str],
    paradigm: int,
) -> dict[str, Any]:
    padded = stems[:4] + [""] * (4 - len(stems))
    numeric_value = 0
    return {
        "schema": COMPILED_SCHEMA,
        "decision_id": decision_id(profile, candidate, method),
        "stems": padded,
        "meaning": clean_meaning(candidate),
        "part_of_speech": PART_CODES[candidate["part_of_speech"]],
        "paradigm": paradigm,
        "class_payload": class_payload(candidate),
        "numeric_value": numeric_value,
        "translation": POC_TRANSLATION,
    }


def headword_proposal(
    profile: str, candidate: dict[str, Any]
) -> tuple[dict[str, Any], str] | None:
    lemma = candidate.get("ascii_lemma", "")
    part = candidate.get("part_of_speech")
    if part not in HEADWORD_ONLY_PARTS or not ASCII_STEM.fullmatch(lemma):
        return None
    paradigm = 0x99 if part in {"NOUN", "ADJ"} else 0
    return compiled_record(profile, candidate, "headword", [lemma], paradigm), "headword"


def empirical_proposal(
    profile: str, candidate: dict[str, Any]
) -> tuple[dict[str, Any], str] | None:
    paradigm = candidate.get("proposed_words_paradigm")
    proposed = candidate.get("proposed_words_stems")
    if not isinstance(paradigm, dict) or not isinstance(proposed, list):
        return None
    stems = [""] * 4
    for item in proposed:
        slot = int(item["slot"])
        stem = str(item["stem"])
        if not 1 <= slot <= 4 or not ASCII_STEM.fullmatch(stem):
            return None
        stems[slot - 1] = stem
    if not any(stems):
        return None
    packed = (
        int(paradigm["declension_or_conjugation"]) << 4
    ) | int(paradigm["variant"])
    method = (
        "empirical-attested"
        if candidate.get("latin_german_form_validation", {}).get("status")
        == "all_slots_attested"
        else "empirical"
    )
    return compiled_record(profile, candidate, method, stems, packed), method


def relaxed_proposal(
    profile: str,
    candidate: dict[str, Any],
    paradigms: dict[str, dict[str, Any]],
    templates: dict[str, dict[str, Any]],
) -> tuple[dict[str, Any], str] | None:
    proposals: list[tuple[int, int, list[str], int]] = []
    for witness in candidate.get("witnesses", []):
        key = witness.get("morphology_key")
        paradigm = paradigms.get(str(key))
        template = templates.get(str(key))
        external = {
            int(item["number"]): str(item["stem"])
            for item in witness.get("external_stems", [])
            if ASCII_STEM.fullmatch(str(item.get("stem", "")))
        }
        if (
            paradigm is None
            or template is None
            or paradigm.get("part_of_speech") != candidate.get("part_of_speech")
        ):
            continue
        stems = [""] * 4
        valid = True
        for item in template["slots"]:
            slot = int(item["words_slot"])
            radical = int(item["external_radical"])
            if radical not in external or not 1 <= slot <= 4:
                valid = False
                break
            stems[slot - 1] = external[radical]
        if not valid or not any(stems):
            continue
        packed = (
            int(paradigm["declension_or_conjugation"]) << 4
        ) | int(paradigm["variant"])
        proposals.append(
            (
                int(paradigm["witnesses"]),
                int(template["witnesses"]),
                stems,
                packed,
            )
        )
    if not proposals:
        return None
    _, _, stems, packed = max(proposals, key=lambda item: (item[0], item[1]))
    return compiled_record(profile, candidate, "majority-map", stems, packed), "majority-map"


def choose_records(
    profile: str,
    candidates: list[dict[str, Any]],
    report: dict[str, Any],
    reference_capacity: int,
    legacy: set[tuple[Any, ...]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], Counter[str]]:
    eligible = [
        item
        for item in candidates
        if item.get("support") == "corroborated_independent"
        and not item.get("proper")
        and item.get("part_of_speech") in PART_CODES
        and ASCII_STEM.fullmatch(str(item.get("ascii_lemma", "")))
    ]
    eligible.sort(key=candidate_priority)
    paradigms = top_paradigms(report)
    templates = top_templates(report)
    proposals: list[tuple[int, tuple[Any, ...], dict[str, Any], str, dict[str, Any]]] = []
    for candidate in eligible:
        if profile == "max-cardinality":
            proposal = headword_proposal(profile, candidate)
            tier = 0 if candidate["part_of_speech"] in INVARIABLE_PARTS else 1
        else:
            proposal = empirical_proposal(profile, candidate)
            tier = 0
            if proposal is None:
                proposal = relaxed_proposal(
                    profile, candidate, paradigms, templates
                )
                tier = 1
            if proposal is None and candidate["part_of_speech"] in INVARIABLE_PARTS:
                proposal = headword_proposal(profile, candidate)
                tier = 2
            if proposal is None:
                proposal = headword_proposal(profile, candidate)
                tier = 3
        if proposal is None:
            continue
        record, method = proposal
        proposals.append((tier, candidate_priority(candidate), record, method, candidate))
    proposals.sort(key=lambda item: (item[0], item[1]))

    selected: list[dict[str, Any]] = []
    selection: list[dict[str, Any]] = []
    counts: Counter[str] = Counter()
    used = 0
    signatures = set(legacy)
    for _, _, record, method, candidate in proposals:
        references = sum(bool(stem) for stem in record["stems"])
        if references == 0 or used + references > reference_capacity:
            counts["skipped_capacity"] += 1
            continue
        key = signature(record)
        if key in signatures:
            counts["skipped_collision"] += 1
            continue
        signatures.add(key)
        selected.append(record)
        used += references
        counts[f"method:{method}"] += 1
        counts[f'part:{candidate["part_of_speech"]}'] += 1
        selection.append(
            {
                "schema": SELECTION_SCHEMA,
                "decision_id": record["decision_id"],
                "ascii_lemma": candidate["ascii_lemma"],
                "part_of_speech": candidate["part_of_speech"],
                "method": method,
                "stem_references": references,
                "support": candidate["support"],
                "independent_families": candidate.get("independent_families", []),
                "source_entries": [
                    {
                        "source": item.get("source"),
                        "source_entry_id": item.get("source_entry_id"),
                    }
                    for item in candidate.get("witnesses", [])
                ],
            }
        )
        if used == reference_capacity:
            break
    if used != reference_capacity:
        raise AppendixError(
            f"could fill only {used} of {reference_capacity} available stem references"
        )
    return selected, selection, counts


def build(arguments: argparse.Namespace) -> dict[str, Any]:
    candidates = list(read_jsonl(arguments.candidates))
    report = json.loads(arguments.audit_report.read_text(encoding="utf-8"))
    dictionary_size = arguments.dictionary.stat().st_size
    stem_size = arguments.stems.stat().st_size
    if dictionary_size % DICTIONARY_RECORD_SIZE or stem_size % STEM_RECORD_SIZE:
        raise AppendixError("unexpected legacy dictionary/stem file size")
    legacy_lexemes = dictionary_size // DICTIONARY_RECORD_SIZE
    legacy_references = stem_size // STEM_RECORD_SIZE
    unique_stems = unique_surfaces(arguments.uniques)
    uniques = count_uniques(arguments.uniques)
    reference_capacity = U16_MAX - legacy_references
    lexeme_capacity = U16_CARDINALITY - legacy_lexemes - uniques
    if reference_capacity <= 0 or lexeme_capacity <= 0:
        raise AppendixError("the legacy dataset has no remaining u16 capacity")

    legacy, legacy_stems = legacy_signatures(arguments.dictionary)
    legacy_stems.update(unique_stems)
    selected, selection, counts = choose_records(
        arguments.profile, candidates, report, reference_capacity, legacy
    )
    if len(selected) > lexeme_capacity:
        raise AppendixError("selection fits stem references but exceeds lexeme capacity")

    output_text = render_jsonl(selected)
    selection_text = render_jsonl(selection)
    arguments.output.write_text(output_text, encoding="utf-8")
    arguments.selection_output.write_text(selection_text, encoding="utf-8")

    imported_stems = {stem for record in selected for stem in record["stems"] if stem}
    summary = {
        "schema": REPORT_SCHEMA,
        "status": "capacity-probe-only",
        "publication_allowed": False,
        "profile": arguments.profile,
        "input_candidates": len(candidates),
        "baseline": {
            "lexemes": legacy_lexemes,
            "uniques": uniques,
            "stem_references": legacy_references,
            "distinct_stem_strings": len(legacy_stems),
        },
        "capacity": {
            "lexemes_available": lexeme_capacity,
            "stem_references_available": reference_capacity,
        },
        "selection": {
            "lexemes": len(selected),
            "stem_references": sum(
                bool(stem) for record in selected for stem in record["stems"]
            ),
            "distinct_stem_strings": len(imported_stems),
            "estimated_total_distinct_stem_strings": len(legacy_stems | imported_stems),
            "by_method": {
                key.removeprefix("method:"): value
                for key, value in sorted(counts.items())
                if key.startswith("method:")
            },
            "by_part_of_speech": {
                key.removeprefix("part:"): value
                for key, value in sorted(counts.items())
                if key.startswith("part:")
            },
            "skipped_collisions": counts["skipped_collision"],
            "skipped_capacity": counts["skipped_capacity"],
        },
        "result": {
            "lexemes_plus_uniques": legacy_lexemes + len(selected) + uniques,
            "stem_references": legacy_references + reference_capacity,
            "stem_reference_counter_remaining": 0,
            "lexeme_counter_remaining": lexeme_capacity - len(selected),
        },
        "output_sha256": "sha256:"
        + hashlib.sha256(output_text.encode("utf-8")).hexdigest(),
        "selection_sha256": "sha256:"
        + hashlib.sha256(selection_text.encode("utf-8")).hexdigest(),
        "limitations": [
            "not an editorial decision ledger",
            "majority-map accepts the most frequent observed mapping even when alternatives exist",
            "headword entries recognize only a deliberately narrow indeclinable/exact form",
            "meanings are unreviewed short source witnesses",
        ],
    }
    arguments.report.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return summary


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidates", type=Path)
    parser.add_argument("audit_report", type=Path)
    parser.add_argument("--dictionary", type=Path, required=True)
    parser.add_argument("--stems", type=Path, required=True)
    parser.add_argument("--uniques", type=Path, required=True)
    parser.add_argument(
        "--profile",
        choices=("max-cardinality", "morphology-first"),
        required=True,
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--selection-output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    summary = build(arguments)
    print(
        f'wrote {summary["selection"]["lexemes"]} lexemes using '
        f'{summary["selection"]["stem_references"]} stem references '
        f'({summary["profile"]})'
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AppendixError, OSError, KeyError, TypeError, ValueError) as error:
        print(f"build_u16_appendix_poc: {error}", file=__import__("sys").stderr)
        raise SystemExit(1) from error
