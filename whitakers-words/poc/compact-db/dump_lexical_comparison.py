#!/usr/bin/env python3

"""Build read-only, homograph-preserving lexical comparison packets.

The output is evidence for editorial reconciliation.  It never mutates a
source database and never promotes a majority vote into the Words database.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import re
import sqlite3
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Iterator, TextIO

import audit_lexical_expansion as expansion
import suggest_quantity_evidence as quantity


SCHEMA = "whitakers-words.lexical-comparison-packet.v1"
PRIMARY_FAMILIES = ("lewis", "gaffiot", "faria")
SOURCE_INFO = {
    "ls_dict": ("lewis", "en", True),
    "gaffiot": ("gaffiot", "fr", True),
    "faria_v3": ("faria", "pt", True),
    # The Latin-German database is an independent morphological validator,
    # but it is not silently turned into a fourth vote in the primary panel.
    "latin_german": ("latin-german", "de", False),
}

PART_ALIASES = {
    "adj": "ADJ",
    "adjective": "ADJ",
    "adv": "ADV",
    "adverb": "ADV",
    "conj": "CONJ",
    "conjunction": "CONJ",
    "interj": "INTERJ",
    "interjection": "INTERJ",
    "noun": "NOUN",
    "num": "NUM",
    "numeral": "NUM",
    "prep": "PREP",
    "preposition": "PREP",
    "pron": "PRON",
    "pronoun": "PRON",
    "proper_noun": "NOUN",
    "subst": "NOUN",
    "v": "VERB",
    "verb": "VERB",
}

AGE = ("unknown", "archaic", "early", "classical", "late", "later", "medieval", "scholarly", "modern")
SUBJECT = ("unknown", "agriculture", "biological-medical", "drama-arts", "ecclesiastic", "grammar-literature", "legal-government", "poetic", "science-philosophy", "technical", "military", "mythology")
GEOGRAPHY = ("unknown", "africa", "britain", "china", "scandinavia", "egypt", "france-gaul", "germany", "greece", "italy-rome", "india", "balkans", "netherlands", "persia", "near-east", "russia", "spain-iberia", "eastern-europe")
FREQUENCY = ("unknown", "very-frequent", "frequent", "common", "lesser", "uncommon", "very-rare", "inscription", "graffiti", "pliny")
WORDS_SOURCE = ("unknown", "source-a", "beeson", "cassells", "adams-latin-sexual-vocabulary", "stelten-ecclesiastical-latin", "deferrari-aquinas", "gildersleeve-lodge", "collatinus", "leverett", "bracton", "calepinus-novus", "lewis-elementary-latin-dictionary", "latham-medieval-word-list", "lynn-nelson", "oxford-latin-dictionary", "souter", "other-dictionaries", "plater-white", "lewis-short", "found-in-translation", "source-u", "saxonis-vademecum", "whitaker", "temporary", "user-submitted")
GENDER = ("unknown", "masculine", "feminine", "neuter", "common")
NOUN_KIND = ("unknown", "singular-only", "plural-only", "abstract", "group", "proper-name", "person", "thing", "locale", "place")
PRONOUN_KIND = ("unknown", "personal", "relative", "reflexive", "demonstrative", "interrogative", "indefinite", "adjectival")
DEGREE = ("unknown", "positive", "comparative", "superlative")
NUMERAL_TYPE = ("unknown", "cardinal", "ordinal", "distributive", "adverbial")
VERB_KIND = ("unknown", "to-be", "compound-of-to-be", "governs-genitive", "governs-dative", "governs-ablative", "transitive", "intransitive", "impersonal", "deponent", "semideponent", "perfect-definite")
CASE = ("unknown", "nominative", "vocative", "genitive", "locative", "dative", "ablative", "accusative")


class ComparisonError(ValueError):
    """An input cannot safely participate in the comparison."""


@dataclass(frozen=True)
class SourceEntry:
    source: str
    source_entry_id: str
    lemma: str
    part_raw: str | None
    part: str | None
    gender_raw: str | None
    gender: str | None
    homograph_number: int | None
    indeclinable: bool | None
    morphology: dict[str, Any]
    meanings: dict[str, Any]
    metadata: dict[str, Any]
    database_key: int | str

    @property
    def family(self) -> str:
        return SOURCE_INFO[self.source][0]

    @property
    def language(self) -> str:
        return SOURCE_INFO[self.source][1]

    @property
    def primary_authority(self) -> bool:
        return SOURCE_INFO[self.source][2]


def enum_name(values: tuple[str, ...], code: int) -> str:
    return values[code] if 0 <= code < len(values) else f"invalid:{code}"


def normalize_part(raw: str | None, gender: str | None = None) -> str | None:
    if raw is None or not raw.strip():
        return "NOUN" if gender else None
    value = raw.strip().lower().replace("é", "e").replace(".", "")
    if value in PART_ALIASES:
        return PART_ALIASES[value]
    if "adj" in value and "num" not in value:
        return "ADJ"
    if value.startswith("v ") or value.startswith("verb"):
        return "VERB"
    if "subst" in value or value.startswith("noun"):
        return "NOUN"
    if "num" in value:
        return "NUM"
    for token, part in (("adv", "ADV"), ("prep", "PREP"), ("pron", "PRON"), ("conj", "CONJ"), ("interj", "INTERJ")):
        if token in value:
            return part
    return None


def normalize_gender(raw: str | None) -> str | None:
    if raw is None:
        return None
    token = raw.strip().lower().rstrip(".")
    aliases = {"m": "m", "masc": "m", "masculine": "m", "f": "f", "fem": "f", "feminine": "f", "n": "n", "neut": "n", "neuter": "n", "c": "c", "common": "c"}
    return aliases.get(token)


def inferred_head_gender(head: str | None) -> str | None:
    if not head:
        return None
    match = re.search(r"(?:^|[\s,;])([mfnc])\.(?:[\s,;]|$)", head.lower())
    return match.group(1) if match else None


def contextual_gender(
    part: str | None, explicit: str | None, head: str | None
) -> str | None:
    if explicit is not None:
        return explicit
    if part not in {None, "NOUN"}:
        return None
    return inferred_head_gender(head)


def sqlite_connection(path: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(quantity.sqlite_uri(path), uri=True)
    connection.row_factory = sqlite3.Row
    return connection


def json_value(value: Any, fallback: Any = None) -> Any:
    if value in (None, ""):
        return fallback
    try:
        return json.loads(str(value))
    except (json.JSONDecodeError, TypeError):
        return {"unparsed": str(value)}


def read_ls_entries(connection: sqlite3.Connection) -> Iterator[SourceEntry]:
    query = """
        select entry_id, id, lemma, homograph_no, pos, gen_text,
               indeclinable, itype, itype_json, head_raw,
               tr_gloss_pt, tr_trad_pt, tr_notas, tr_citacoes
          from entry order by entry_id
    """
    for row in connection.execute(query):
        preliminary_part = normalize_part(row["pos"])
        gender = contextual_gender(preliminary_part, None, row["head_raw"])
        yield SourceEntry(
            "ls_dict", str(row["id"] or row["entry_id"]), str(row["lemma"]),
            row["pos"], normalize_part(row["pos"], gender), gender, gender,
            row["homograph_no"], bool(row["indeclinable"]),
            {"genitive_or_governed_case": row["gen_text"], "inflection_type": row["itype"], "inflection_type_tokens": json_value(row["itype_json"], [])},
            {"head": row["head_raw"], "gloss_pt": row["tr_gloss_pt"], "definition_pt": row["tr_trad_pt"], "translator_notes": row["tr_notas"]},
            {"translation_citations": json_value(row["tr_citacoes"], [])},
            int(row["entry_id"]),
        )


def read_gaffiot_entries(connection: sqlite3.Connection) -> Iterator[SourceEntry]:
    query = """
        select entry_id, lemma, homograph_no, pos, gender, indeclinable,
               itype, itype_json, gen_text, pron, etym, head_raw
          from entry order by entry_id
    """
    for row in connection.execute(query):
        preliminary_part = normalize_part(row["pos"])
        gender = contextual_gender(
            preliminary_part, normalize_gender(row["gender"]), row["head_raw"]
        )
        yield SourceEntry(
            "gaffiot", str(row["entry_id"]), str(row["lemma"]), row["pos"],
            normalize_part(row["pos"], gender), row["gender"], gender,
            row["homograph_no"], bool(row["indeclinable"]),
            {"genitive_or_governed_case": row["gen_text"], "inflection_type": row["itype"], "inflection_type_tokens": json_value(row["itype_json"], []), "pronunciation": row["pron"]},
            {"head": row["head_raw"], "etymology": row["etym"]}, {},
            int(row["entry_id"]),
        )


def read_faria_entries(connection: sqlite3.Connection) -> Iterator[SourceEntry]:
    query = """
        select entry_id, headword, homograph_number, morphology_raw,
               part_of_speech_raw, lexical_pos_norm, lexical_pos_source,
               definition_raw, etymology_raw, confidence_raw,
               provenance_json, legacy_entry_id, source_text
          from entry
         where entry_kind = 'lexical_entry' and editorial_status = 'publishable'
         order by sort_order
    """
    for row in connection.execute(query):
        raw_part = row["lexical_pos_norm"] or row["part_of_speech_raw"]
        gender = inferred_head_gender(row["morphology_raw"])
        yield SourceEntry(
            "faria_v3", str(row["entry_id"]), str(row["headword"]), raw_part,
            normalize_part(raw_part, gender), gender, gender,
            row["homograph_number"], None,
            {"raw": row["morphology_raw"], "part_of_speech_source": row["lexical_pos_source"]},
            {"definition": row["definition_raw"], "etymology": row["etymology_raw"]},
            {"confidence": row["confidence_raw"], "legacy_entry_id": row["legacy_entry_id"], "provenance": json_value(row["provenance_json"], {}), "source_text": row["source_text"]},
            str(row["entry_id"]),
        )


def read_latin_german_entries(connection: sqlite3.Connection) -> Iterator[SourceEntry]:
    parts = {"a": "ADJ", "s": "NOUN", "v": "VERB"}
    query = """
        select id, vok_id, latin, desc, html, key, grammar, typnr
          from VOC where grammar in ('a', 's', 'v') order by id
    """
    for row in connection.execute(query):
        grammar = str(row["grammar"])
        latin = str(row["latin"] or "")
        lemma = quantity.latin_german_lemma(latin, grammar)
        gender = quantity.latin_german_gender(latin) if grammar == "s" else None
        yield SourceEntry(
            "latin_german", str(row["vok_id"] or f'VOC:{row["id"]}'), lemma,
            grammar, parts[grammar], gender, gender, None, None,
            {"citation": latin, "type_number": row["typnr"]},
            {"definition": row["desc"], "html": row["html"]},
            {"lookup_key": row["key"]}, int(row["id"]),
        )


def detailed_entry(entry: SourceEntry, connection: sqlite3.Connection, include_forms: bool) -> dict[str, Any]:
    morphology = dict(entry.morphology)
    meanings = dict(entry.meanings)
    metadata = dict(entry.metadata)
    if entry.source in {"ls_dict", "gaffiot"}:
        language_column = "gloss_en" if entry.source == "ls_dict" else "gloss_fr"
        meanings["senses"] = [dict(row) for row in connection.execute(
            f"select sense_id, parent_id, n_label, level, {language_column} as gloss, gloss_raw from sense where entry_id = ? order by sense_id",
            (entry.database_key,),
        )]
        morphology["basic_forms"] = [dict(row) for row in connection.execute(
            "select form, form_norm, kind from entry_form "
            "where entry_id = ? and kind in ('orth', 'itype') order by form_id",
            (entry.database_key,),
        )]
        if include_forms:
            morphology["forms"] = [dict(row) for row in connection.execute(
                "select form, form_norm, kind from entry_form where entry_id = ? order by form_id",
                (entry.database_key,),
            )]
        if entry.source == "gaffiot":
            metadata["special"] = [dict(row) for row in connection.execute(
                "select key, value, ord from entry_meta where entry_id = ? order by ord, meta_id",
                (entry.database_key,),
            )]
        metadata["cross_references"] = [
            dict(row)
            for row in connection.execute(
                "select target_text, target_norm, rel_type from crossref "
                "where entry_id = ? order by crossref_id",
                (entry.database_key,),
            )
        ]
    elif entry.source == "faria_v3":
        meanings["senses"] = [
            dict(row)
            for row in connection.execute(
                "select sense_id, sequence_no, label_raw, definition_raw "
                "from sense where entry_id = ? order by sequence_no, sense_id",
                (entry.database_key,),
            )
        ]
        morphology["basic_forms"] = [
            dict(row)
            for row in connection.execute(
                "select form, form_search, is_primary from entry_form "
                "where entry_id = ? and is_primary = 1 order by sequence_no, form_id",
                (entry.database_key,),
            )
        ]
        if include_forms:
            morphology["forms"] = [
                dict(row)
                for row in connection.execute(
                    "select form, form_search, is_primary from entry_form "
                    "where entry_id = ? order by sequence_no, form_id",
                    (entry.database_key,),
                )
            ]
        morphology["grammatical_categories"] = [
            {
                **dict(row),
                "evidence": json_value(row["evidence_json"], {}),
            }
            for row in connection.execute(
                "select category_kind, category_key, value_norm, source_code, "
                "confidence, evidence_json from entry_grammatical_category "
                "where entry_id = ? order by category_kind, category_key, value_norm",
                (entry.database_key,),
            )
        ]
        metadata["notes"] = [
            dict(row)
            for row in connection.execute(
                "select sequence_no, text from note where entry_id = ? "
                "order by sequence_no, note_id",
                (entry.database_key,),
            )
        ]
        metadata["cross_references"] = [
            dict(row)
            for row in connection.execute(
                "select sequence_no, label_raw, target_raw, target_search, "
                "target_entry_id, resolution_status, relation_kind, is_pure_alias "
                "from cross_reference where source_entry_id = ? "
                "order by sequence_no, cross_reference_id",
                (entry.database_key,),
            )
        ]
    elif entry.source == "latin_german" and include_forms:
        vok_id = entry.source_entry_id
        morphology["grammar_forms"] = [dict(row) for row in connection.execute(
            "select nr, form, form_norm from GRAMMAR where vok_id = ? order by id", (vok_id,)
        )]
        morphology["analyzed_forms"] = [dict(row) for row in connection.execute(
            "select form, form_norm, bestimmung as analysis from FORM where vok_id = ? order by id", (vok_id,)
        )]

    word = quantity.extract_first_word(entry.lemma, require_quantity=False)
    assert word is not None
    return {
        "source": entry.source,
        "source_family": entry.family,
        "language": entry.language,
        "primary_consensus_authority": entry.primary_authority,
        "source_entry_id": entry.source_entry_id,
        "homograph_number": entry.homograph_number,
        "citation": {
            "source": entry.lemma,
            "ascii": word.source,
            "proper": word.proper,
            "quantity_observations": quantity_observations(word),
        },
        "lexical": {
            "part_of_speech_raw": entry.part_raw,
            "part_of_speech": entry.part,
            "gender_raw": entry.gender_raw,
            "gender": entry.gender,
            "indeclinable": entry.indeclinable,
        },
        "morphology": morphology,
        "meanings": meanings,
        "metadata_and_flags": metadata,
    }


def quantity_observations(word: quantity.MarkedWord) -> list[dict[str, Any]]:
    return [
        {"position": index, "letter": word.source[index], "quantity": "long" if mark == quantity.MACRON else "short"}
        for index, mark in enumerate(word.marks) if mark is not None
    ]


def read_words_quantities(path: Path | None) -> dict[tuple[int, int], tuple[int, int]]:
    result: dict[tuple[int, int], tuple[int, int]] = {}
    if path is None or not path.exists():
        return result
    # QUANTITIES.LAT has an ASCII structural prefix, but its trailing evidence
    # comments may quote marked Unicode lemmas.
    for line_number, source_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        fields = source_line.split()
        if not fields:
            continue
        if len(fields) != 5 or fields[0] != "STEM":
            continue
        key = (int(fields[1]), int(fields[2]))
        if key in result:
            raise ComparisonError(f"{path}:{line_number}: duplicate STEM quantity")
        result[key] = (int(fields[3]), int(fields[4]))
    return result


def mask_observations(stem: str, masks: tuple[int, int] | None) -> list[dict[str, Any]]:
    if masks is None:
        return []
    known, long_vowels = masks
    return [
        {"position": position, "letter": letter, "quantity": "long" if long_vowels & (1 << position) else "short"}
        for position, letter in enumerate(stem) if known & (1 << position)
    ]


def read_words_entries(dictionary: Path, quantities_path: Path | None) -> dict[int, dict[str, Any]]:
    data = dictionary.read_bytes()
    if len(data) % quantity.DICTIONARY_RECORD_SIZE:
        raise ComparisonError("DICTFILE.GEN size is not a multiple of 180 bytes")
    masks = read_words_quantities(quantities_path)
    result: dict[int, dict[str, Any]] = {}
    for zero_id in range(len(data) // quantity.DICTIONARY_RECORD_SIZE):
        entry_id = zero_id + 1
        record = data[zero_id * 180:(zero_id + 1) * 180]
        stems = [quantity.decode_ascii(record[index * 18:(index + 1) * 18], f"entry {entry_id} stem {index + 1}") for index in range(4)]
        part_code = record[72]
        part = quantity.PART_NAMES.get(part_code, "OTHER")
        which = int.from_bytes(record[76:80], "little") if part_code in {1, 2, 3, 4, 5, 7, 8, 9} else 0
        variant = int.from_bytes(record[80:84], "little") if which or part_code in {1, 2, 3, 4, 5, 7, 8, 9} else 0
        attribute_code = record[84] if part_code in {1, 2, 3, 4, 5, 7} else (record[76] if part_code in {6, 10} else 0)
        attribute_name = None
        if part_code == 1:
            attribute_name = enum_name(GENDER, attribute_code)
        elif part_code in {2, 3}:
            attribute_name = enum_name(PRONOUN_KIND, attribute_code)
        elif part_code in {4, 6}:
            attribute_name = enum_name(DEGREE, attribute_code)
        elif part_code == 5:
            attribute_name = enum_name(NUMERAL_TYPE, attribute_code)
        elif part_code == 7:
            attribute_name = enum_name(VERB_KIND, attribute_code)
        elif part_code == 10:
            attribute_name = enum_name(CASE, attribute_code)
        noun_kind_code = record[85] if part_code == 1 else 0
        metadata_codes = {"age": record[92], "subject": record[93], "geography": record[94], "frequency": record[95], "source": record[96]}
        result[entry_id] = {
            "entry_id": entry_id,
            "stems": [
                {"slot": slot, "ascii": stem, "quantity_observations": mask_observations(stem.lower(), masks.get((entry_id, slot)))}
                for slot, stem in enumerate(stems, 1) if stem
            ],
            "part_of_speech": part,
            "part_of_speech_code": part_code,
            "paradigm": {"declension_or_conjugation": which, "variant": variant},
            "class_attribute": {"code": attribute_code, "name": attribute_name},
            "noun_kind": {"code": noun_kind_code, "name": enum_name(NOUN_KIND, noun_kind_code)} if part_code == 1 else None,
            "numeral_value": int.from_bytes(record[88:92], "little") if part_code == 5 else None,
            "meaning": quantity.decode_ascii(record[97:177], f"entry {entry_id} meaning"),
            "metadata_and_flags": {
                "codes": metadata_codes,
                "age": enum_name(AGE, metadata_codes["age"]),
                "subject": enum_name(SUBJECT, metadata_codes["subject"]),
                "geography": enum_name(GEOGRAPHY, metadata_codes["geography"]),
                "frequency": enum_name(FREQUENCY, metadata_codes["frequency"]),
                "source": enum_name(WORDS_SOURCE, metadata_codes["source"]),
            },
        }
    return result


def source_match_ids(stems: dict[str, tuple[quantity.WhitakerStem, ...]], entry: SourceEntry) -> list[int]:
    word = quantity.extract_first_word(entry.lemma, require_quantity=False)
    if word is None:
        return []
    dictionary_entry = quantity.DictionaryEntry(
        entry.source, entry.source_entry_id, entry.lemma, entry.part,
        entry.gender, str(entry.meanings.get("head") or entry.meanings.get("definition") or entry.lemma),
        entry.family, True,
    )
    item = expansion.NormalizedEntry(dictionary_entry, word)
    return sorted({match.dictionary_entry for match in expansion.find_matches(stems, item)})


def field_vote(values: dict[str, Any]) -> dict[str, Any]:
    counts = Counter(value for value in values.values() if value is not None)
    winners = [value for value, count in counts.items() if count >= 2]
    if len(winners) == 1:
        status, value = "majority_2_of_3", winners[0]
    elif len(counts) <= 1 and counts:
        status, value = "agreement_without_quorum", next(iter(counts))
    elif counts:
        status, value = "conflict", None
    else:
        status, value = "unknown", None
    return {"status": status, "value": value, "votes": values}


def quantity_vote(entries: list[tuple[SourceEntry, quantity.MarkedWord]]) -> list[dict[str, Any]]:
    by_position: dict[int, dict[str, str]] = defaultdict(dict)
    family_conflicts: set[tuple[str, int]] = set()
    for entry, word in entries:
        if not entry.primary_authority:
            continue
        for position, mark in enumerate(word.marks):
            if mark is None:
                continue
            value = "long" if mark == quantity.MACRON else "short"
            old = by_position[position].get(entry.family)
            if old is not None and old != value:
                family_conflicts.add((entry.family, position))
            by_position[position][entry.family] = value
    output = []
    for position in sorted(by_position):
        votes = dict(sorted(by_position[position].items()))
        conflicts = sorted(family for family, at in family_conflicts if at == position)
        for family in conflicts:
            votes.pop(family, None)
        decision = field_vote(votes)
        output.append({"position": position, **decision, "internally_conflicted_families": conflicts})
    return output


def entries_by_effective_part(
    entries: list[SourceEntry],
) -> dict[str, list[SourceEntry]]:
    known_parts = {entry.part for entry in entries if entry.part is not None}
    by_part: dict[str, list[SourceEntry]] = defaultdict(list)
    for entry in entries:
        part = entry.part
        if part is None and len(known_parts) == 1:
            # An untyped witness may join the only POS attested for this
            # spelling.  It remains untyped in the factual source record.
            part = next(iter(known_parts))
        if part is not None:
            by_part[part].append(entry)
    return by_part


def semantic_alignment_required(entries: list[SourceEntry]) -> bool:
    return any(
        count > 1
        for members in entries_by_effective_part(entries).values()
        for count in Counter(member.family for member in members).values()
    )


def consensus_preview(entries: list[SourceEntry]) -> list[dict[str, Any]]:
    by_part = entries_by_effective_part(entries)
    previews = []
    for part, members in sorted(by_part.items()):
        by_family: dict[str, list[SourceEntry]] = defaultdict(list)
        for member in members:
            by_family[member.family].append(member)
        primary_by_family = {
            family: items
            for family, items in by_family.items()
            if items[0].primary_authority
        }
        duplicates = {
            family: len(items) for family, items in by_family.items() if len(items) > 1
        }
        primary_duplicates = {
            family: count
            for family, count in duplicates.items()
            if family in primary_by_family
        }
        eligible = len(primary_by_family) >= 2 and not primary_duplicates
        if primary_duplicates:
            status = "blocked_on_semantic_alignment"
        elif len(primary_by_family) < 2:
            status = "insufficient_primary_sources"
        else:
            status = "eligible_unique_entry_per_family"
        preview: dict[str, Any] = {
            "part_of_speech": part,
            "status": status,
            "primary_families_present": sorted(primary_by_family),
            "multiple_entries_by_family": dict(sorted(duplicates.items())),
            "primary_multiple_entries_by_family": dict(
                sorted(primary_duplicates.items())
            ),
            "auxiliary_alignment_required": bool(
                set(duplicates) - set(primary_duplicates)
            ),
            "automatic_promotion_allowed": False,
        }
        if eligible:
            unique = [items[0] for items in primary_by_family.values()]
            preview["fields"] = {
                "gender": field_vote({item.family: item.gender for item in unique}),
                "indeclinable": field_vote({item.family: item.indeclinable for item in unique}),
            }
            marked = [(item, quantity.extract_first_word(item.lemma, require_quantity=False)) for item in unique]
            preview["vowel_quantity"] = quantity_vote([(item, word) for item, word in marked if word is not None])
        previews.append(preview)
    return previews


def revision(record: dict[str, Any]) -> str:
    payload = json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "sha256:" + hashlib.sha256(payload.encode("utf-8")).hexdigest()


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def sqlite_schema_signature(path: Path) -> str | None:
    with path.open("rb") as source:
        if source.read(16) != b"SQLite format 3\x00":
            return None
    connection = sqlite_connection(path)
    try:
        rows = [
            tuple(row)
            for row in connection.execute(
                "select type,name,tbl_name,coalesce(sql,'') from sqlite_master "
                "order by type,name,tbl_name"
            )
        ]
    finally:
        connection.close()
    payload = json.dumps(rows, ensure_ascii=False, separators=(",", ":"))
    return "sha256:" + hashlib.sha256(payload.encode("utf-8")).hexdigest()


def source_manifest(arguments: argparse.Namespace) -> list[dict[str, Any]]:
    paths = {
        "words_dictionary": arguments.dictionary,
        "words_quantities": arguments.quantities,
        "ls_dict": arguments.ls_database,
        "gaffiot": arguments.gaffiot_database,
        "faria_v3": arguments.faria_v3,
        "latin_german": arguments.latin_german,
    }
    result = []
    for source_key, path in paths.items():
        if path is None:
            continue
        family, language, primary = SOURCE_INFO.get(
            source_key, ("current-words", "en", False)
        )
        record = {
                "source_key": source_key,
                "source_family": family,
                "language": language,
                "primary_authority": primary,
                "path": str(path.resolve()),
                "size_bytes": path.stat().st_size,
                "sha256": file_digest(path),
            }
        schema_signature = sqlite_schema_signature(path)
        if schema_signature is not None:
            record["schema_signature"] = schema_signature
        result.append(record)
    return result


def open_output(path: Path) -> TextIO:
    if path.suffix == ".gz":
        return gzip.open(path, "wt", encoding="utf-8", newline="")
    return path.open("w", encoding="utf-8", newline="")


def parse_lemma_filter(values: Iterable[str]) -> set[str]:
    result = set()
    for value in values:
        word = quantity.extract_first_word(value, require_quantity=False)
        if word is None or word.source != re.sub(r"[^a-z]", "", value.lower()):
            raise ComparisonError(f"--lemma must be one simple Latin word: {value!r}")
        result.add(word.source)
    return result


def build(arguments: argparse.Namespace) -> dict[str, Any]:
    source_paths = {
        "ls_dict": arguments.ls_database,
        "gaffiot": arguments.gaffiot_database,
        "faria_v3": arguments.faria_v3,
        "latin_german": arguments.latin_german,
    }
    readers = {
        "ls_dict": read_ls_entries,
        "gaffiot": read_gaffiot_entries,
        "faria_v3": read_faria_entries,
        "latin_german": read_latin_german_entries,
    }
    connections = {name: sqlite_connection(path) for name, path in source_paths.items() if path is not None}
    try:
        groups: dict[tuple[str, bool], list[SourceEntry]] = defaultdict(list)
        skipped = Counter()
        filters = parse_lemma_filter(arguments.lemma)
        for source, connection in connections.items():
            for entry in readers[source](connection):
                word = quantity.extract_first_word(entry.lemma, require_quantity=False)
                if word is None:
                    skipped[f"{source}:non_simple_headword"] += 1
                    continue
                if filters and word.source not in filters:
                    continue
                groups[(word.source, word.proper)].append(entry)

        selected_parts = set(arguments.parts)
        if selected_parts:
            groups = {
                key: entries
                for key, entries in groups.items()
                if any(entry.part in selected_parts for entry in entries)
            }

        words = read_words_entries(arguments.dictionary, arguments.quantities)
        word_stems = quantity.read_whitaker_stems(arguments.dictionary)
        status_counts = Counter()
        preview_counts = Counter()
        source_counts = Counter()
        source_content: dict[str, Counter[str]] = defaultdict(Counter)
        matched_words: set[int] = set()
        alignment_groups = 0
        with open_output(arguments.output) as output:
            for (ascii_lemma, proper), entries in sorted(groups.items()):
                external = []
                group_matches: set[int] = set()
                for entry in sorted(entries, key=lambda item: (item.source, item.source_entry_id)):
                    record = detailed_entry(entry, connections[entry.source], arguments.include_forms)
                    matches = source_match_ids(word_stems, entry)
                    record["current_words_match_ids"] = matches
                    group_matches.update(matches)
                    source_counts[entry.source] += 1
                    source_content[entry.source]["quantity_observations"] += len(
                        record["citation"]["quantity_observations"]
                    )
                    source_content[entry.source]["forms"] += len(
                        record["morphology"].get("forms", [])
                    )
                    source_content[entry.source]["basic_forms"] += len(
                        record["morphology"].get("basic_forms", [])
                    )
                    source_content[entry.source]["grammar_forms"] += len(
                        record["morphology"].get("grammar_forms", [])
                    )
                    source_content[entry.source]["analyzed_forms"] += len(
                        record["morphology"].get("analyzed_forms", [])
                    )
                    source_content[entry.source]["senses"] += len(
                        record["meanings"].get("senses", [])
                    )
                    source_content[entry.source]["grammatical_categories"] += len(
                        record["morphology"].get("grammatical_categories", [])
                    )
                    source_content[entry.source]["cross_references"] += len(
                        record["metadata_and_flags"].get("cross_references", [])
                    )
                    source_content[entry.source]["notes"] += len(
                        record["metadata_and_flags"].get("notes", [])
                    )
                    external.append(record)
                matched_words.update(group_matches)
                coverage = "present" if group_matches else "absent_from_current_words"
                if len(group_matches) > 1:
                    coverage = "ambiguous_current_words_matches"
                status_counts[coverage] += 1
                previews = consensus_preview(entries)
                preview_counts.update(item["status"] for item in previews)
                needs_alignment = semantic_alignment_required(entries)
                alignment_groups += needs_alignment
                packet = {
                    "schema": SCHEMA,
                    "key": {"ascii_lemma": ascii_lemma, "proper": proper},
                    "comparison": {
                        "current_words_coverage": coverage,
                        "current_words_match_ids": sorted(group_matches),
                        "homographs_preserved": True,
                        "semantic_alignment_required_before_consensus": needs_alignment,
                    },
                    "current_words_entries": [words[entry_id] for entry_id in sorted(group_matches)],
                    "source_entries": external,
                    "consensus_preview": previews,
                    "policy": {
                        "primary_panel": list(PRIMARY_FAMILIES),
                        "latin_german_role": "morphological_validation_and_support",
                        "unmarked_vowel_means": "unknown",
                        "majority_scope": "one_semantically_aligned_lexeme_and_one_vote_per_source_family",
                        "automatic_promotion_allowed": False,
                    },
                }
                packet["revision"] = revision(packet)
                output.write(json.dumps(packet, ensure_ascii=False, separators=(",", ":")) + "\n")
    finally:
        for connection in connections.values():
            connection.close()

    return {
        "schema": "whitakers-words.lexical-comparison-report.v2",
        "output": str(arguments.output),
        "include_all_inflected_forms": arguments.include_forms,
        "selected_parts": sorted(selected_parts) or "all",
        "groups": len(groups),
        "source_entries": dict(sorted(source_counts.items())),
        "source_content": {
            source: dict(sorted(counts.items()))
            for source, counts in sorted(source_content.items())
        },
        "coverage": dict(sorted(status_counts.items())),
        "consensus_preview": dict(sorted(preview_counts.items())),
        "groups_requiring_semantic_alignment": alignment_groups,
        "current_words_entries_matched": len(matched_words),
        "current_words_entries_total": len(words),
        "skipped": dict(sorted(skipped.items())),
        "source_manifest": source_manifest(arguments),
        "output_sha256": file_digest(arguments.output),
        "read_policy": "SQLite immutable=1; source databases were not modified",
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dictionary", type=Path, help="Whitaker DICTFILE.GEN")
    parser.add_argument("--ls-database", type=Path)
    parser.add_argument("--gaffiot-database", type=Path)
    parser.add_argument("--faria-v3", type=Path)
    parser.add_argument("--latin-german", type=Path)
    parser.add_argument("--quantities", type=Path, help="optional current QUANTITIES.LAT")
    parser.add_argument("--lemma", action="append", default=[], help="limit to one simple ASCII lemma; repeatable")
    parser.add_argument(
        "--part",
        action="append",
        choices=sorted(expansion.KNOWN_PARTS),
        dest="parts",
        default=[],
        help="retain groups containing this POS; repeatable (default: all)",
    )
    parser.add_argument("--include-forms", action=argparse.BooleanOptionalAction, default=True, help="include every source inflected form (default: true)")
    parser.add_argument("--output", type=Path, required=True, help="JSONL or JSONL.GZ packet output")
    parser.add_argument("--report", type=Path, required=True, help="summary JSON")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if not any((arguments.ls_database, arguments.gaffiot_database, arguments.faria_v3, arguments.latin_german)):
        raise ComparisonError("at least one external source database is required")
    report = build(arguments)
    arguments.report.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {report['groups']} comparison packets to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
