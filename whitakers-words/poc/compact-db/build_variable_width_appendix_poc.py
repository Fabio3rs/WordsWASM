#!/usr/bin/env python3

"""Build read-only-source lexical appendix probes with 16/18/19-bit IDs.

The input is the unmatched-candidate JSONL emitted by
``audit_lexical_expansion.py``.  Lewis & Short and Gaffiot are the primary
families; Faria is retained as a lower-priority scanned authority.  Other
families may help reconstruct a Whitaker-compatible paradigm, but can never
create an entry by themselves.

The ``.wwax`` files are experimental appendix containers, not WWDB files and
not publication material.  They intentionally omit definitions and retain
only lemma, lexical class, declension/conjugation, variant, gender/properness,
up to four stems, prefix boundaries, and a provenance sidecar.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import zlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Iterator, Sequence

import build_u16_appendix_poc as u16


FORMAT = "whitakers-words.variable-width-appendix-poc.v1"
REPORT_FORMAT = "whitakers-words.variable-width-appendix-report.v1"
SELECTION_FORMAT = "whitakers-words.variable-width-appendix-selection.v1"
MAGIC = b"WWAPX\r\n\x1a"
FORMAT_VERSION = 1
PREFIX_SIZE = 20
DICTIONARY_RECORD_SIZE = 180
STEM_RECORD_SIZE = 56
STEM_SIZE = 18
STEM_BUCKETS = 703
BOUNDARY_COUNT = STEM_BUCKETS + 1
SUPPORTED_WIDTHS = (16, 18, 19)
TRUSTED_FAMILIES = frozenset({"lewis", "gaffiot", "faria"})
PRIMARY_FAMILIES = frozenset({"lewis", "gaffiot"})
ASCII_LEMMA = re.compile(r"[a-z]{1,18}\Z")
LATIN_GERMAN_PROVENANCE = {
    "repository": "https://github.com/hackerpschorr/Latin-GermanDictionary",
    "license": "GPL-3.0-only",
    "declared_contents": "Latin-German Dictionary (SQLite)",
    "lexicographic_source_declared": False,
    "poc_role": "auxiliary-morphology-only",
}


class ProbeError(ValueError):
    """The requested appendix cannot be represented by the probe format."""


@dataclass(frozen=True)
class ExtractedLexeme:
    candidate: dict[str, Any]
    lemma: str
    part: str
    part_code: int
    proper: bool
    gender: int
    declension_or_conjugation: int
    variant: int
    stems: tuple[str, str, str, str]
    method: str
    source_tier: str
    source_rank: int

    @property
    def metadata(self) -> int:
        return (
            self.part_code
            | (self.declension_or_conjugation << 4)
            | (self.variant << 8)
            | (self.gender << 12)
            | (int(self.proper) << 15)
        )

    @property
    def stem_references(self) -> int:
        return sum(bool(stem) for stem in self.stems)


def read_jsonl(path: Path) -> Iterator[dict[str, Any]]:
    yield from u16.read_jsonl(path)


def canonical_json(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def render_jsonl(records: Iterable[dict[str, Any]]) -> str:
    return "".join(canonical_json(record).decode("utf-8") + "\n" for record in records)


def fixed_ascii(record: bytes, offset: int, size: int) -> str:
    try:
        return record[offset : offset + size].decode("ascii").rstrip(" \x00")
    except UnicodeDecodeError as error:
        raise ProbeError("legacy lexical data is not ASCII") from error


def intern(pool: dict[str, int], values: list[str], value: str) -> int:
    found = pool.get(value)
    if found is not None:
        return found
    identifier = len(values)
    pool[value] = identifier
    values.append(value)
    return identifier


def legacy_string_pool(
    dictionary: Path, uniques: Path
) -> tuple[dict[str, int], list[str]]:
    data = dictionary.read_bytes()
    if len(data) % DICTIONARY_RECORD_SIZE:
        raise ProbeError(f"{dictionary}: unexpected DICTFILE.GEN size")
    pool: dict[str, int] = {}
    values: list[str] = []
    for offset in range(0, len(data), DICTIONARY_RECORD_SIZE):
        record = data[offset : offset + DICTIONARY_RECORD_SIZE]
        for slot in range(4):
            intern(pool, values, fixed_ascii(record, slot * STEM_SIZE, STEM_SIZE))
    for surface in u16.unique_data_lines(uniques)[::3]:
        intern(pool, values, surface)
    return pool, values


def normalized_bucket_letter(character: str) -> str:
    lowered = character.lower()
    if lowered == "j":
        return "i"
    if lowered == "v":
        return "u"
    return lowered


def stem_bucket(stem: str) -> int:
    if not stem:
        return 0
    first_character = normalized_bucket_letter(stem[0])
    if not "a" <= first_character <= "z":
        raise ProbeError(f"stem outside a-z prefix index: {stem}")
    first = ord(first_character) - ord("a")
    base = 1 + first * 27
    if len(stem) == 1:
        return base
    second_character = normalized_bucket_letter(stem[1])
    if not "a" <= second_character <= "z":
        raise ProbeError(f"stem outside a-z prefix index: {stem}")
    return base + 1 + ord(second_character) - ord("a")


def legacy_bucket_counts(stems: Path) -> list[int]:
    data = stems.read_bytes()
    if len(data) % STEM_RECORD_SIZE:
        raise ProbeError(f"{stems}: unexpected STEMFILE.GEN size")
    counts = [0] * STEM_BUCKETS
    for offset in range(0, len(data), STEM_RECORD_SIZE):
        stem = fixed_ascii(data, offset, STEM_SIZE)
        counts[stem_bucket(stem)] += 1
    return counts


def source_policy(candidate: dict[str, Any]) -> tuple[int, str] | None:
    families = {
        str(witness.get("source_family"))
        for witness in candidate.get("witnesses", [])
    }
    trusted = families & TRUSTED_FAMILIES
    if not trusted:
        return None
    if PRIMARY_FAMILIES <= trusted:
        return 0, "lewis-short+gaffiot"
    if trusted & PRIMARY_FAMILIES and "faria" in trusted:
        return 1, "primary+faria-scan"
    if trusted & PRIMARY_FAMILIES:
        return 2, "single-primary"
    return 3, "faria-scan-only"


def proposal_for(
    candidate: dict[str, Any],
    paradigms: dict[str, dict[str, Any]],
    templates: dict[str, dict[str, Any]],
) -> tuple[tuple[str, str, str, str], int, int, str]:
    empirical = u16.empirical_proposal("variable-width", candidate)
    if empirical is not None:
        record, method = empirical
        paradigm = int(record["paradigm"])
        return tuple(record["stems"]), paradigm >> 4, paradigm & 0x0F, method

    relaxed = u16.relaxed_proposal(
        "variable-width", candidate, paradigms, templates
    )
    if relaxed is not None:
        record, _ = relaxed
        paradigm = int(record["paradigm"])
        return (
            tuple(record["stems"]),
            paradigm >> 4,
            paradigm & 0x0F,
            "auxiliary-majority-map",
        )

    proposed = candidate.get("proposed_words_paradigm")
    if isinstance(proposed, dict):
        return (
            (str(candidate["ascii_lemma"]), "", "", ""),
            int(proposed["declension_or_conjugation"]),
            int(proposed["variant"]),
            "mapped-class-headword-stem",
        )

    return (
        (str(candidate["ascii_lemma"]), "", "", ""),
        0,
        0,
        "unmapped-headword-stem",
    )


def extract_candidates(
    candidates: Sequence[dict[str, Any]], report: dict[str, Any]
) -> tuple[list[ExtractedLexeme], Counter[str]]:
    paradigms = u16.top_paradigms(report)
    templates = u16.top_templates(report)
    extracted: list[ExtractedLexeme] = []
    counts: Counter[str] = Counter()
    seen: set[tuple[Any, ...]] = set()
    for candidate in candidates:
        policy = source_policy(candidate)
        if policy is None:
            counts["excluded_without_trusted_source"] += 1
            continue
        lemma = str(candidate.get("ascii_lemma", ""))
        part = str(candidate.get("part_of_speech", ""))
        if not ASCII_LEMMA.fullmatch(lemma) or part not in u16.PART_CODES:
            counts["excluded_unrepresentable"] += 1
            continue
        stems, declension, variant, method = proposal_for(
            candidate, paradigms, templates
        )
        if (
            len(stems) != 4
            or not all(not stem or u16.ASCII_STEM.fullmatch(stem) for stem in stems)
            or not 0 <= declension <= 15
            or not 0 <= variant <= 15
        ):
            counts["excluded_unrepresentable"] += 1
            continue
        genders = candidate.get("genders", [])
        gender = u16.GENDERS.get(genders[0], 0) if len(genders) == 1 else 0
        source_rank, tier = policy
        key = (lemma, part, bool(candidate.get("proper")), declension, variant)
        if key in seen:
            counts["excluded_duplicate_structural_key"] += 1
            continue
        seen.add(key)
        extracted.append(
            ExtractedLexeme(
                candidate=candidate,
                lemma=lemma,
                part=part,
                part_code=u16.PART_CODES[part],
                proper=bool(candidate.get("proper")),
                gender=gender,
                declension_or_conjugation=declension,
                variant=variant,
                stems=stems,
                method=method,
                source_tier=tier,
                source_rank=source_rank,
            )
        )
        counts[f"eligible_source_tier:{tier}"] += 1
        counts[f"eligible_method:{method}"] += 1
    extracted.sort(
        key=lambda item: (
            item.source_rank,
            item.method == "unmapped-headword-stem",
            item.proper,
            u16.candidate_priority(item.candidate),
            item.declension_or_conjugation,
            item.variant,
        )
    )
    return extracted, counts


def encode_fixed_records(rows: Iterable[Sequence[tuple[int, int]]]) -> bytes:
    output = bytearray()
    for row in rows:
        accumulator = 0
        shift = 0
        for value, width in row:
            if width <= 0 or value < 0 or value >= 1 << width:
                raise ProbeError(f"value {value} does not fit {width} bits")
            accumulator |= value << shift
            shift += width
        output.extend(accumulator.to_bytes((shift + 7) // 8, "little"))
    return bytes(output)


def decode_fixed_record(
    data: bytes, index: int, widths: Sequence[int]
) -> tuple[int, ...]:
    bits = sum(widths)
    stride = (bits + 7) // 8
    offset = index * stride
    if offset + stride > len(data):
        raise ProbeError("record is outside encoded data")
    accumulator = int.from_bytes(data[offset : offset + stride], "little")
    result: list[int] = []
    shift = 0
    for width in widths:
        result.append((accumulator >> shift) & ((1 << width) - 1))
        shift += width
    if accumulator >> shift:
        raise ProbeError("record has nonzero padding bits")
    return tuple(result)


def inspect_image(path: Path) -> dict[str, Any]:
    """Parse and fully verify a generated WWAX image."""
    image = path.read_bytes()
    if len(image) < PREFIX_SIZE:
        raise ProbeError("truncated WWAX prefix")
    magic, version, width, flags, manifest_size, expected_crc = struct.unpack(
        "<8sBBHII", image[:PREFIX_SIZE]
    )
    if magic != MAGIC or version != FORMAT_VERSION or width not in SUPPORTED_WIDTHS:
        raise ProbeError("unsupported WWAX prefix")
    manifest_end = PREFIX_SIZE + manifest_size
    if manifest_end > len(image):
        raise ProbeError("truncated WWAX manifest")
    try:
        manifest = json.loads(image[PREFIX_SIZE:manifest_end])
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProbeError("invalid WWAX manifest") from error
    payload = image[manifest_end:]
    if manifest.get("schema") != FORMAT or manifest.get("id_bits") != width:
        raise ProbeError("WWAX prefix/manifest mismatch")
    if len(payload) != manifest.get("payload_bytes"):
        raise ProbeError("WWAX payload length mismatch")
    actual_crc = zlib.crc32(payload)
    if actual_crc != expected_crc or f"{actual_crc:08x}" != manifest.get(
        "payload_crc32"
    ):
        raise ProbeError("WWAX payload CRC mismatch")

    cursor = 0
    sections: dict[str, bytes] = {}
    for section in manifest.get("sections", []):
        offset = int(section["offset"])
        size = int(section["bytes"])
        if offset != cursor or size < 0 or offset + size > len(payload):
            raise ProbeError("invalid WWAX section extent")
        data = payload[offset : offset + size]
        if hashlib.sha256(data).hexdigest() != section.get("sha256"):
            raise ProbeError("WWAX section digest mismatch")
        sections[str(section["name"])] = data
        cursor += size
    if cursor != len(payload):
        raise ProbeError("WWAX sections do not cover payload")

    layout = manifest["layout"]
    appendix = manifest["appendix"]
    expected_lengths = {
        "lexeme_records": appendix["lexemes"] * layout["lexeme_record_stride"],
        "stem_references": appendix["stem_references"]
        * layout["stem_reference_stride"],
        "combined_prefix_boundaries": BOUNDARY_COUNT * layout["boundary_stride"],
    }
    for name, expected in expected_lengths.items():
        if len(sections.get(name, b"")) != expected:
            raise ProbeError(f"unexpected {name} length")

    string_count = 0
    string_data = sections.get("new_string_pool", b"")
    cursor = 0
    while cursor < len(string_data):
        size = string_data[cursor]
        cursor += 1 + size
        if cursor > len(string_data):
            raise ProbeError("truncated WWAX string pool")
        string_count += 1
    if string_count != appendix["new_strings"]:
        raise ProbeError("WWAX string count mismatch")
    return {
        "id_bits": width,
        "flags": flags,
        "bytes": len(image),
        "manifest": manifest,
    }


def select_for_width(
    width: int,
    extracted: Sequence[ExtractedLexeme],
    base_pool: dict[str, int],
    base_values: Sequence[str],
    *,
    base_lexemes: int,
    uniques: int,
    base_references: int,
) -> tuple[list[ExtractedLexeme], dict[str, int], list[str], Counter[str]]:
    cardinality = 1 << width
    maximum = cardinality - 1
    pool = dict(base_pool)
    values = list(base_values)
    selected: list[ExtractedLexeme] = []
    counts: Counter[str] = Counter()
    references = base_references
    for item in extracted:
        strings = (item.lemma, *item.stems)
        additions = tuple(
            dict.fromkeys(value for value in strings if value not in pool)
        )
        if base_lexemes + len(selected) + 1 + uniques > cardinality:
            counts["skipped_lexeme_capacity"] += 1
            continue
        if references + item.stem_references > maximum:
            counts["skipped_reference_capacity"] += 1
            continue
        if len(values) + len(additions) > cardinality:
            counts["skipped_string_capacity"] += 1
            continue
        for value in additions:
            intern(pool, values, value)
        selected.append(item)
        references += item.stem_references
    return selected, pool, values, counts


def selection_record(item: ExtractedLexeme, lexeme_id: int) -> dict[str, Any]:
    trusted = [
        witness
        for witness in item.candidate.get("witnesses", [])
        if witness.get("source_family") in TRUSTED_FAMILIES
    ]
    auxiliary = [
        witness
        for witness in item.candidate.get("witnesses", [])
        if witness.get("source_family") not in TRUSTED_FAMILIES
    ]
    return {
        "schema": SELECTION_FORMAT,
        "lexeme_id": lexeme_id,
        "lemma": item.lemma,
        "part_of_speech": item.part,
        "proper": item.proper,
        "declension_or_conjugation": item.declension_or_conjugation,
        "variant": item.variant,
        "gender": item.gender,
        "stems": list(item.stems),
        "method": item.method,
        "source_tier": item.source_tier,
        "absence_basis": "no-compatible-whitaker-citation-stem",
        "trusted_witnesses": [
            {
                "source": witness.get("source"),
                "source_family": witness.get("source_family"),
                "source_entry_id": witness.get("source_entry_id"),
                "lemma": witness.get("lemma"),
                "morphology_hint": witness.get("morphology_hint"),
            }
            for witness in trusted
        ],
        "auxiliary_morphology_witnesses": [
            {
                "source": witness.get("source"),
                "source_family": witness.get("source_family"),
                "source_entry_id": witness.get("source_entry_id"),
                "morphology_key": witness.get("morphology_key"),
            }
            for witness in auxiliary
            if witness.get("morphology_key") is not None
        ],
    }


def encode_profile(
    output_directory: Path,
    width: int,
    selected: Sequence[ExtractedLexeme],
    pool: dict[str, int],
    values: Sequence[str],
    base_bucket_counts: Sequence[int],
    *,
    base_lexemes: int,
    uniques: int,
    base_references: int,
    base_strings: int,
    capacity_counts: Counter[str],
) -> dict[str, Any]:
    profile_directory = output_directory / f"u{width}"
    profile_directory.mkdir(parents=True, exist_ok=True)
    record_widths = (width, width, width, width, width, 16)
    record_rows: list[list[tuple[int, int]]] = []
    references: list[tuple[int, str, int, int]] = []
    selections: list[dict[str, Any]] = []
    by_method: Counter[str] = Counter()
    by_source_tier: Counter[str] = Counter()
    by_part: Counter[str] = Counter()
    by_auxiliary_family: Counter[str] = Counter()
    imported_bucket_counts = [0] * STEM_BUCKETS
    for index, item in enumerate(selected):
        lexeme_id = base_lexemes + index
        ids = [pool[item.lemma], *(pool[stem] for stem in item.stems)]
        record_rows.append(
            [(identifier, width) for identifier in ids] + [(item.metadata, 16)]
        )
        for slot, stem in enumerate(item.stems):
            if not stem:
                continue
            bucket = stem_bucket(stem)
            imported_bucket_counts[bucket] += 1
            references.append((bucket, stem, lexeme_id, slot))
        selections.append(selection_record(item, lexeme_id))
        by_method[item.method] += 1
        by_source_tier[item.source_tier] += 1
        by_part[item.part] += 1
        by_auxiliary_family.update(
            {
                str(witness.get("source_family"))
                for witness in item.candidate.get("witnesses", [])
                if witness.get("source_family") not in TRUSTED_FAMILIES
                and witness.get("morphology_key") is not None
            }
        )

    record_data = encode_fixed_records(record_rows)
    references.sort()
    reference_data = encode_fixed_records(
        [
            (
                (lexeme_id, width),
                (slot, 2),
                (slot + 1, 3),
            )
            for _, _, lexeme_id, slot in references
        ]
    )
    cumulative = 0
    boundaries = [0]
    for legacy, imported in zip(base_bucket_counts, imported_bucket_counts):
        cumulative += legacy + imported
        boundaries.append(cumulative)
    boundary_data = encode_fixed_records([((value, width),) for value in boundaries])

    new_values = values[base_strings:]
    string_data = bytearray()
    for value in new_values:
        encoded = value.encode("ascii")
        if len(encoded) > 255:
            raise ProbeError("appendix string exceeds u8 length")
        string_data.append(len(encoded))
        string_data.extend(encoded)

    sections = {
        "lexeme_records": record_data,
        "stem_references": reference_data,
        "combined_prefix_boundaries": boundary_data,
        "new_string_pool": bytes(string_data),
    }
    offset = 0
    section_manifest: list[dict[str, Any]] = []
    payload_parts: list[bytes] = []
    for name, data in sections.items():
        section_manifest.append(
            {
                "name": name,
                "offset": offset,
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
        payload_parts.append(data)
        offset += len(data)
    payload = b"".join(payload_parts)
    manifest = {
        "schema": FORMAT,
        "publication_allowed": False,
        "id_bits": width,
        "layout": {
            "lexeme_record": (
                f"lemma_id:{width} | stem_id[4]:{width} each | "
                "pos:4 | class:4 | variant:4 | gender:3 | proper:1"
            ),
            "lexeme_record_bits": sum(record_widths),
            "lexeme_record_stride": (sum(record_widths) + 7) // 8,
            "stem_reference": (
                f"lexeme_id:{width} | lexical_slot:2 | stem_key:3"
            ),
            "stem_reference_bits": width + 5,
            "stem_reference_stride": (width + 5 + 7) // 8,
            "boundary_bits": width,
            "boundary_stride": (width + 7) // 8,
        },
        "baseline": {
            "lexemes": base_lexemes,
            "uniques": uniques,
            "stem_references": base_references,
            "strings": base_strings,
        },
        "appendix": {
            "lexemes": len(selected),
            "stem_references": len(references),
            "new_strings": len(new_values),
        },
        "combined": {
            "lexemes_plus_uniques": base_lexemes + len(selected) + uniques,
            "stem_references": cumulative,
            "strings": len(values),
        },
        "sections": section_manifest,
        "payload_bytes": len(payload),
        "payload_crc32": f"{zlib.crc32(payload):08x}",
    }
    manifest_data = canonical_json(manifest)
    prefix = struct.pack(
        "<8sBBHII",
        MAGIC,
        FORMAT_VERSION,
        width,
        0x0007,
        len(manifest_data),
        zlib.crc32(payload),
    )
    if len(prefix) != PREFIX_SIZE:
        raise AssertionError("unexpected appendix prefix size")
    image = prefix + manifest_data + payload
    image_path = profile_directory / f"lexical-appendix-u{width}.wwax"
    selection_path = profile_directory / "selection.jsonl"
    report_path = profile_directory / "report.json"
    image_path.write_bytes(image)
    verified_image = inspect_image(image_path)
    selection_text = render_jsonl(selections)
    selection_path.write_text(selection_text, encoding="utf-8")

    report = {
        "schema": REPORT_FORMAT,
        "publication_allowed": False,
        "id_bits": width,
        "source_policy": {
            "primary": ["lewis", "gaffiot"],
            "scanned_secondary": ["faria"],
            "auxiliary_can_create_entry": False,
            "latin_german": LATIN_GERMAN_PROVENANCE,
        },
        "selection": {
            "lexemes": len(selected),
            "stem_references": len(references),
            "new_strings": len(new_values),
            "by_method": dict(sorted(by_method.items())),
            "by_source_tier": dict(sorted(by_source_tier.items())),
            "by_part_of_speech": dict(sorted(by_part.items())),
            "with_auxiliary_morphology_by_family": dict(
                sorted(by_auxiliary_family.items())
            ),
            "capacity_skips": dict(sorted(capacity_counts.items())),
        },
        "combined": manifest["combined"],
        "binary": {
            "path": str(image_path),
            "bytes": len(image),
            "manifest_bytes": len(manifest_data),
            "payload_bytes": len(payload),
            "sections": section_manifest,
            "sha256": hashlib.sha256(image).hexdigest(),
            "verified": verified_image["bytes"] == len(image),
        },
        "selection_sha256": hashlib.sha256(
            selection_text.encode("utf-8")
        ).hexdigest(),
        "limitations": [
            "candidate absence is structural and does not prove semantic "
            "distinctness",
            "definitions and editorial meanings are intentionally omitted",
            "unmapped headwords preserve POS but class 0/variant 0 is "
            "explicitly unknown",
            "auxiliary morphology may rank or reconstruct a selected "
            "trusted-source lemma but cannot create one",
            "WWAX is an experimental appendix container and is not accepted "
            "by the WWDB runtime",
        ],
    }
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return report


def build(arguments: argparse.Namespace) -> dict[str, Any]:
    widths = sorted(set(arguments.width))
    if not widths or any(width not in SUPPORTED_WIDTHS for width in widths):
        raise ProbeError(f"widths must be selected from {SUPPORTED_WIDTHS}")
    candidates = list(read_jsonl(arguments.candidates))
    audit_report = json.loads(arguments.audit_report.read_text(encoding="utf-8"))
    dictionary_size = arguments.dictionary.stat().st_size
    stem_size = arguments.stems.stat().st_size
    if dictionary_size % DICTIONARY_RECORD_SIZE or stem_size % STEM_RECORD_SIZE:
        raise ProbeError("unexpected legacy input size")
    base_lexemes = dictionary_size // DICTIONARY_RECORD_SIZE
    base_references = stem_size // STEM_RECORD_SIZE
    uniques = u16.count_uniques(arguments.uniques)
    base_pool, base_values = legacy_string_pool(arguments.dictionary, arguments.uniques)
    base_bucket_counts = legacy_bucket_counts(arguments.stems)
    extracted, extraction_counts = extract_candidates(candidates, audit_report)
    arguments.output_directory.mkdir(parents=True, exist_ok=True)

    reports: list[dict[str, Any]] = []
    for width in widths:
        selected, pool, values, capacity_counts = select_for_width(
            width,
            extracted,
            base_pool,
            base_values,
            base_lexemes=base_lexemes,
            uniques=uniques,
            base_references=base_references,
        )
        reports.append(
            encode_profile(
                arguments.output_directory,
                width,
                selected,
                pool,
                values,
                base_bucket_counts,
                base_lexemes=base_lexemes,
                uniques=uniques,
                base_references=base_references,
                base_strings=len(base_values),
                capacity_counts=capacity_counts,
            )
        )

    summary = {
        "schema": REPORT_FORMAT,
        "publication_allowed": False,
        "input_candidates": len(candidates),
        "eligible_trusted_source_candidates": len(extracted),
        "extraction_counts": dict(sorted(extraction_counts.items())),
        "baseline": {
            "lexemes": base_lexemes,
            "uniques": uniques,
            "stem_references": base_references,
            "strings": len(base_values),
        },
        "profiles": [
            {
                "id_bits": report["id_bits"],
                "selection": report["selection"],
                "combined": report["combined"],
                "binary": report["binary"],
            }
            for report in reports
        ],
    }
    (arguments.output_directory / "summary.json").write_text(
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
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(
        "--width",
        type=int,
        action="append",
        choices=SUPPORTED_WIDTHS,
        help="repeat to select profiles; defaults to 16, 18 and 19",
    )
    arguments = parser.parse_args()
    arguments.width = arguments.width or list(SUPPORTED_WIDTHS)
    return arguments


def main() -> int:
    arguments = parse_arguments()
    summary = build(arguments)
    for profile in summary["profiles"]:
        print(
            f'u{profile["id_bits"]}: '
            f'{profile["selection"]["lexemes"]} appendix lexemes, '
            f'{profile["combined"]["stem_references"]} combined references, '
            f'{profile["binary"]["bytes"]} bytes'
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
