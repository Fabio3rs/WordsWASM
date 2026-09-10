#!/usr/bin/env python3
"""Validate and compile the human-reviewed grammar specialization ledger."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


SCHEMA = "words.grammar-specialization"
TRACE_SCHEMA = "words.grammar-trace"
KINDS = {"code-specialization", "assessment", "rewrite-family"}
COMPATIBILITY = {
    "differentially-equal",
    "intentional-difference",
}
GRAMMAR_STATUSES = {"linguistically-supported", "disputed", "legacy-bug"}
EXPECTATIONS = {"analysis", "compound", "no-compound", "flag"}
WWDB_MAGIC = b"WWDB\r\n\x1a\n"
WWDB_FIXED_HEADER_SIZE = 40
WWDB_DIRECTORY_ENTRY_SIZE = 32
WWDB_INFLECTION_SECTION = 7
WWDB_ROW_MAJOR = 1
WWDB_COLUMNAR = 2
WWDB_INFLECTION_STRIDE = 6
LEGACY_INFLECTION_STRIDE = 40
PROFILE_CODES = {"simple": 1, "dense": 2, "columnar": 3, "search-only": 4}
PARADIGM_PARTS = {1, 2, 3, 4, 5, 7, 8, 9}


def load_manifest(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        clean = line.strip()
        if not clean:
            continue
        try:
            record = json.loads(clean)
        except json.JSONDecodeError as error:
            raise ValueError(f"invalid JSON on manifest line {line_number}") from error
        if not isinstance(record, dict):
            raise ValueError(f"manifest line {line_number} is not an object")
        records.append(record)
    return records


def _nonempty_string(value: object) -> bool:
    return isinstance(value, str) and bool(value.strip())


def validate_manifest(root: Path, records: list[dict[str, Any]]) -> None:
    identities: set[str] = set()
    for index, record in enumerate(records, 1):
        identity = record.get("id")
        if not _nonempty_string(identity):
            raise ValueError(f"specialization {index} has no stable id")
        if identity in identities:
            raise ValueError(f"duplicate specialization: {identity}")
        identities.add(identity)

        if record.get("schema") != SCHEMA or record.get("schemaVersion") != 1:
            raise ValueError(f"invalid schema for specialization: {identity}")
        if record.get("kind") not in KINDS:
            raise ValueError(f"invalid kind for specialization: {identity}")
        if record.get("compatibility") not in COMPATIBILITY:
            raise ValueError(f"invalid compatibility for specialization: {identity}")
        if record.get("grammarStatus") not in GRAMMAR_STATUSES:
            raise ValueError(f"invalid grammar status for specialization: {identity}")
        if not _nonempty_string(record.get("claim")):
            raise ValueError(f"missing claim for specialization: {identity}")

        implementations = record.get("implementation")
        if not isinstance(implementations, list) or not implementations:
            raise ValueError(f"missing implementation for specialization: {identity}")
        for implementation in implementations:
            if not isinstance(implementation, dict):
                raise ValueError(f"invalid implementation for specialization: {identity}")
            path = implementation.get("path")
            symbol = implementation.get("symbol")
            if not _nonempty_string(path) or not (root / path).is_file():
                raise ValueError(f"implementation source does not exist: {path}")
            if not _nonempty_string(symbol):
                raise ValueError(f"implementation symbol is missing: {identity}")

        grammar = record.get("grammar")
        if not isinstance(grammar, list) or not grammar:
            raise ValueError(f"missing grammar evidence for specialization: {identity}")
        for evidence in grammar:
            if not isinstance(evidence, dict) or not all(
                _nonempty_string(evidence.get(field))
                for field in ("authority", "locator", "claim")
            ):
                raise ValueError(f"invalid grammar evidence for specialization: {identity}")

        witnesses = record.get("witnesses")
        if not isinstance(witnesses, list) or not witnesses:
            raise ValueError(f"missing witnesses for specialization: {identity}")
        for witness in witnesses:
            if (
                not isinstance(witness, dict)
                or not _nonempty_string(witness.get("input"))
                or witness.get("expect") not in EXPECTATIONS
            ):
                raise ValueError(f"invalid witness for specialization: {identity}")


def _sha256(path: Path) -> str:
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def _uint(data: bytes, offset: int, width: int) -> int:
    return int.from_bytes(data[offset:offset + width], "little")


def _packed_record(
    image: bytes, payload: int, count: int, flags: int, rule_id: int
) -> bytes:
    if flags == WWDB_ROW_MAJOR:
        begin = payload + rule_id * WWDB_INFLECTION_STRIDE
        return image[begin:begin + WWDB_INFLECTION_STRIDE]
    if flags == WWDB_COLUMNAR:
        return bytes(
            image[payload + byte_index * count + rule_id]
            for byte_index in range(WWDB_INFLECTION_STRIDE)
        )
    raise ValueError(f"unsupported WWDB inflection layout flag: {flags}")


def _source_morphology(record: bytes, part_of_speech: int) -> int:
    if part_of_speech in {1, 2, 3, 9}:
        return record[12] | (record[13] << 3) | (record[14] << 5)
    if part_of_speech in {4, 5}:
        return (
            record[12]
            | (record[13] << 3)
            | (record[14] << 5)
            | (record[15] << 8)
        )
    if part_of_speech in {6, 10}:
        return record[4]
    if part_of_speech == 7:
        return (
            record[12]
            | (record[13] << 3)
            | (record[14] << 5)
            | (record[15] << 8)
            | (record[16] << 10)
        )
    if part_of_speech == 8:
        return (
            record[12]
            | (record[13] << 3)
            | (record[14] << 5)
            | (record[15] << 8)
            | (record[16] << 11)
            | (record[17] << 13)
        )
    return 0


def compile_inflection_trace(root: Path, database: Path) -> dict[str, Any]:
    image = database.read_bytes()
    if image[:len(WWDB_MAGIC)] != WWDB_MAGIC:
        raise ValueError("grammar trace database is not WWDB")
    section_count = _uint(image, 16, 4)
    inflection_section: tuple[int, int, int, int] | None = None
    for index in range(section_count):
        begin = WWDB_FIXED_HEADER_SIZE + index * WWDB_DIRECTORY_ENTRY_SIZE
        section_type = _uint(image, begin, 4)
        if section_type == WWDB_INFLECTION_SECTION:
            inflection_section = (
                _uint(image, begin + 4, 4),
                _uint(image, begin + 8, 8),
                _uint(image, begin + 24, 4),
                _uint(image, begin + 28, 4),
            )
            break
    if inflection_section is None:
        raise ValueError("WWDB has no inflection section")
    flags, payload, count, stride = inflection_section
    if stride != WWDB_INFLECTION_STRIDE:
        raise ValueError("grammar trace requires compact u48 inflections")

    source = (root / "whitakers-words/INFLECTS.SEC").read_bytes()
    if len(source) % LEGACY_INFLECTION_STRIDE != 0:
        raise ValueError("INFLECTS.SEC has a partial record")
    source_records = [
        (index, source[index * LEGACY_INFLECTION_STRIDE:(index + 1) * LEGACY_INFLECTION_STRIDE])
        for index in range(len(source) // LEGACY_INFLECTION_STRIDE)
        if source[index * LEGACY_INFLECTION_STRIDE] != 0
    ]
    if len(source_records) != count:
        raise ValueError("INFLECTS.SEC and WWDB rule counts differ")

    ending_ids: dict[str, int] = {}
    for _, record in source_records:
        ending_size = _uint(record, 24, 4)
        ending = record[28:28 + ending_size].decode("ascii")
        if ending not in ending_ids:
            ending_ids[ending] = len(ending_ids)

    records: list[dict[str, Any]] = []
    for rule_id, (source_index, legacy) in enumerate(source_records):
        packed_bytes = _packed_record(image, payload, count, flags, rule_id)
        if len(packed_bytes) != WWDB_INFLECTION_STRIDE:
            raise ValueError("truncated WWDB inflection section")
        packed = int.from_bytes(packed_bytes, "little")
        fields = {
            "partOfSpeech": {"offset": 0, "width": 4, "value": packed & 0xF},
            "paradigm": {"offset": 4, "width": 8, "value": (packed >> 4) & 0xFF},
            "morphology": {"offset": 12, "width": 16, "value": (packed >> 12) & 0xFFFF},
            "endingStringId": {"offset": 28, "width": 9, "value": (packed >> 28) & 0x1FF},
            "stemKeyMinusOne": {"offset": 37, "width": 2, "value": (packed >> 37) & 0x3},
            "age": {"offset": 39, "width": 4, "value": (packed >> 39) & 0xF},
            "frequency": {"offset": 43, "width": 4, "value": (packed >> 43) & 0xF},
            "reserved": {"offset": 47, "width": 1, "value": (packed >> 47) & 0x1},
        }
        source_part = legacy[0]
        source_which = _uint(legacy, 4, 4) if source_part in PARADIGM_PARTS else 0
        source_variant = _uint(legacy, 8, 4) if source_part in PARADIGM_PARTS else 0
        source_paradigm = (source_which << 4) | source_variant
        source_morphology = _source_morphology(legacy, source_part)
        source_stem_key = _uint(legacy, 20, 4)
        ending_size = _uint(legacy, 24, 4)
        ending = legacy[28:28 + ending_size].decode("ascii")
        if (
            fields["partOfSpeech"]["value"] != source_part
            or fields["paradigm"]["value"] != source_paradigm
            or fields["morphology"]["value"] != source_morphology
            or fields["endingStringId"]["value"] != ending_ids[ending]
            or fields["stemKeyMinusOne"]["value"] + 1 != source_stem_key
            or fields["age"]["value"] != legacy[36]
            or fields["frequency"]["value"] != legacy[37]
            or fields["reserved"]["value"] != 0
        ):
            raise ValueError(f"inflection transport mismatch at RuleId {rule_id}")
        records.append({
            "ruleId": rule_id,
            "source": {
                "path": "whitakers-words/INFLECTS.SEC",
                "recordIndex": source_index,
                "byteOffset": source_index * LEGACY_INFLECTION_STRIDE,
                "digest": "sha256:" + hashlib.sha256(legacy).hexdigest(),
                "ending": ending,
                "partOfSpeech": source_part,
                "paradigm": source_paradigm,
                "morphology": source_morphology,
                "stemKey": source_stem_key,
                "age": legacy[36],
                "frequency": legacy[37],
            },
            "packed": {
                "section": "inflections",
                "recordIndex": rule_id,
                "bytes": packed_bytes.hex(),
                "usedBits": 47,
                "fields": fields,
            },
            "runtime": {"type": "words::RuleId", "value": rule_id},
        })
    return {
        "count": count,
        "sourceDigest": _sha256(root / "whitakers-words/INFLECTS.SEC"),
        "wireLayout": "columnar-u48" if flags == WWDB_COLUMNAR else "row-major-u48",
        "records": records,
    }


def compile_trace(
    root: Path,
    records: list[dict[str, Any]],
    database: Path | None = None,
    profile: str | None = None,
) -> str:
    validate_manifest(root, records)
    if (database is None) != (profile is None):
        raise ValueError("database and profile must be supplied together")
    paths = sorted(
        {
            implementation["path"]
            for record in records
            for implementation in record["implementation"]
        }
    )
    trace = {
        "schema": TRACE_SCHEMA,
        "schemaVersion": 1,
        "counts": {
            "specializations": len(records),
            "witnesses": sum(len(record["witnesses"]) for record in records),
            "implementationSources": len(paths),
        },
        "sources": [
            {"path": path, "digest": _sha256(root / path)} for path in paths
        ],
        "specializations": sorted(records, key=lambda record: record["id"]),
    }
    if database is not None:
        image = database.read_bytes()
        expected_profile = PROFILE_CODES.get(profile)
        if expected_profile is None or _uint(image, 20, 4) != expected_profile:
            raise ValueError("declared profile does not match WWDB header")
        trace["dataset"] = {
            "profile": profile,
            "bytes": database.stat().st_size,
            "digest": _sha256(database),
        }
        trace["inflections"] = compile_inflection_trace(root, database)
    return json.dumps(trace, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--database", type=Path)
    parser.add_argument("--profile")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", type=Path)
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    manifest = arguments.manifest or root / "whitakers-words/GRAMMAR_SPECIALIZATIONS.jsonl"
    compiled = compile_trace(
        root,
        load_manifest(manifest),
        arguments.database,
        arguments.profile,
    )
    if arguments.check is not None:
        if arguments.check.read_text(encoding="utf-8") != compiled:
            raise ValueError(f"grammar trace differs: {arguments.check}")
    elif arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(compiled, encoding="utf-8")
    else:
        print(compiled, end="")


if __name__ == "__main__":
    main()
