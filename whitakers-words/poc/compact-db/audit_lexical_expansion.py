#!/usr/bin/env python3

"""Measure conservative lexical expansion candidates for Whitaker's Words."""

from __future__ import annotations

import argparse
import itertools
import json
import re
import sqlite3
from collections import Counter, defaultdict
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Iterable

import suggest_quantity_evidence as quantity


KNOWN_PARTS = frozenset(quantity.CITATION_ENDINGS)
FARIA_PARTS = {
    "ADJECTIVE": "ADJ",
    "ADVERB": "ADV",
    "CONJUNCTION": "CONJ",
    "INTERJECTION": "INTERJ",
    "NOUN": "NOUN",
    "NUMERAL": "NUM",
    "PREPOSITION": "PREP",
    "PRONOUN": "PRON",
    "PROPER_NOUN": "NOUN",
    "VERB": "VERB",
}
INFLECTION_RECORD_SIZE = 40


@dataclass(frozen=True)
class NormalizedEntry:
    dictionary: quantity.DictionaryEntry
    word: quantity.MarkedWord
    inferred_part: bool = False

    def group_key(self) -> tuple[str, str, bool]:
        part = self.dictionary.part_of_speech
        if part not in KNOWN_PARTS:
            raise quantity.SuggestionError("a lexical group requires a known POS")
        return (self.word.source, part, self.word.proper)


def find_matches(
    stems: dict[str, tuple[quantity.WhitakerStem, ...]],
    item: NormalizedEntry,
    *,
    maximum_ending: int = 6,
) -> tuple[quantity.WhitakerStem, ...]:
    """Find conservative citation-form matches against Words slot 1."""

    word = item.word
    entry = item.dictionary
    result: dict[tuple[int, int], quantity.WhitakerStem] = {}
    minimum = max(1, len(word.source) - maximum_ending)
    for length in range(minimum, min(quantity.STEM_SIZE, len(word.source)) + 1):
        for stem in stems.get(word.source[:length], ()):
            # WHY: a headword can establish coverage only through the citation
            # stem. A prefix collision with another principal part is not
            # evidence that Words already represents the same lexeme.
            if (
                stem.slot != 1
                or word.proper != stem.proper
                or not quantity.citation_ending_matches(word, stem)
                or not quantity.parts_compatible(
                    entry.part_of_speech, stem.part_of_speech
                )
                or not quantity.genders_compatible(entry.gender, stem.gender)
            ):
                continue
            result[(stem.dictionary_entry, stem.slot)] = stem
    return tuple(result[key] for key in sorted(result))


def normalize_entries(
    entries: Iterable[quantity.DictionaryEntry],
) -> tuple[tuple[NormalizedEntry, ...], tuple[NormalizedEntry, ...], Counter[str]]:
    known: list[NormalizedEntry] = []
    unknown: list[NormalizedEntry] = []
    counts: Counter[str] = Counter()
    for entry in entries:
        counts["entries"] += 1
        word = quantity.extract_first_word(entry.lemma, require_quantity=False)
        if word is None:
            counts["excluded_non_simple_headword"] += 1
            continue
        counts["simple_headwords"] += 1
        item = NormalizedEntry(entry, word)
        if entry.part_of_speech in KNOWN_PARTS:
            known.append(item)
            counts["known_pos"] += 1
        else:
            unknown.append(item)
            counts["unknown_pos"] += 1
    return tuple(known), tuple(unknown), counts


def attach_unambiguous_unknown_pos(
    known: tuple[NormalizedEntry, ...],
    unknown: tuple[NormalizedEntry, ...],
) -> tuple[tuple[NormalizedEntry, ...], tuple[NormalizedEntry, ...]]:
    parts_by_headword: dict[tuple[str, bool], set[str]] = defaultdict(set)
    for item in known:
        part = item.dictionary.part_of_speech
        assert part is not None
        parts_by_headword[(item.word.source, item.word.proper)].add(part)

    attached: list[NormalizedEntry] = list(known)
    unresolved: list[NormalizedEntry] = []
    for item in unknown:
        parts = parts_by_headword.get((item.word.source, item.word.proper), set())
        if len(parts) != 1:
            unresolved.append(item)
            continue
        # WHY: an untyped witness may corroborate an already identified
        # headword/POS, but it must not create a POS or vote for several POS
        # alternatives. This retains Gaffiot coverage without inventing data.
        inferred = replace(item.dictionary, part_of_speech=next(iter(parts)))
        attached.append(NormalizedEntry(inferred, item.word, inferred_part=True))
    return tuple(attached), tuple(unresolved)


def source_id(entry: quantity.DictionaryEntry) -> str:
    return quantity.SOURCE_EVIDENCE_IDS[entry.source_name]


def read_faria_v3_entries(database: Path) -> Iterable[quantity.DictionaryEntry]:
    connection = sqlite3.connect(quantity.sqlite_uri(database), uri=True)
    try:
        columns = {row[1] for row in connection.execute("pragma table_info(entry)")}
        required = {
            "entry_id",
            "entry_kind",
            "editorial_status",
            "headword",
            "morphology_raw",
            "lexical_pos_norm",
            "definition_raw",
        }
        if not required <= columns:
            missing = ", ".join(sorted(required - columns))
            raise quantity.SuggestionError(f"Faria entry table is missing: {missing}")
        query = """
            select entry_id, headword, morphology_raw, lexical_pos_norm,
                   definition_raw
              from entry
             where entry_kind = 'lexical_entry'
               and editorial_status = 'publishable'
             order by sort_order
        """
        for (
            identifier,
            headword,
            morphology,
            source_part,
            definition,
        ) in connection.execute(query):
            part = FARIA_PARTS.get(str(source_part or ""))
            gender_match = re.search(r"\b([mfn])\.", str(morphology or "").lower())
            gender = gender_match.group(1) if gender_match is not None else None
            family, independent = quantity.SOURCE_FAMILIES["faria_v3"]
            yield quantity.DictionaryEntry(
                source_name="faria_v3",
                source_entry_id=str(identifier),
                lemma=str(headword),
                part_of_speech=part,
                gender=gender if part == "NOUN" else None,
                head="; ".join(
                    piece
                    for piece in (str(headword), str(morphology), str(definition))
                    if piece
                ),
                source_family=family,
                independent_quantity_authority=independent,
            )
    finally:
        connection.close()


def witness_record(item: NormalizedEntry) -> dict[str, Any]:
    record = {
        "source": source_id(item.dictionary),
        "source_family": item.dictionary.source_family,
        "independent_authority": item.dictionary.independent_quantity_authority,
        "source_entry_id": item.dictionary.source_entry_id,
        "lemma": item.dictionary.lemma,
        "part_of_speech_inferred": item.inferred_part,
        "head": quantity.short_witness(item.dictionary.head, item.dictionary.lemma),
    }
    if item.dictionary.morphology_hint is not None:
        record["morphology_hint"] = item.dictionary.morphology_hint
    if item.dictionary.morphology_key is not None:
        record["morphology_key"] = item.dictionary.morphology_key
    if item.dictionary.external_stems:
        record["external_stems"] = [
            {"number": number, "stem": stem}
            for number, stem in item.dictionary.external_stems
        ]
    return record


def paradigm_record(paradigm: tuple[str, int, int]) -> dict[str, Any]:
    part, declension, variant = paradigm
    return {
        "part_of_speech": part,
        "declension_or_conjugation": declension,
        "variant": variant,
    }


def build_morphology_crosswalk(
    observations: dict[str, Counter[tuple[str, int, int]]],
    ambiguous_witnesses: Counter[str],
    unmatched_witnesses: Counter[str],
    *,
    minimum_support: int = 3,
) -> tuple[dict[str, Any], dict[str, tuple[str, int, int]]]:
    mappings: list[dict[str, Any]] = []
    resolved: dict[str, tuple[str, int, int]] = {}
    status_counts: Counter[str] = Counter()
    all_keys = sorted(
        set(observations) | set(ambiguous_witnesses) | set(unmatched_witnesses)
    )
    for morphology_key in all_keys:
        counts = observations.get(morphology_key, Counter())
        supported = sum(counts.values())
        if len(counts) == 1 and supported >= minimum_support:
            status = "exact_empirical"
            resolved[morphology_key] = next(iter(counts))
        elif len(counts) == 1:
            status = "insufficient_support"
        elif counts:
            status = "one_to_many"
        else:
            status = "no_covered_witness"
        status_counts[status] += 1
        mappings.append(
            {
                "morphology_key": morphology_key,
                "status": status,
                "unambiguous_covered_witnesses": supported,
                "ambiguous_covered_witnesses": ambiguous_witnesses[morphology_key],
                "unmatched_witnesses": unmatched_witnesses[morphology_key],
                "observed_paradigms": [
                    {
                        **paradigm_record(paradigm),
                        "witnesses": count,
                    }
                    for paradigm, count in sorted(
                        counts.items(), key=lambda item: (-item[1], item[0])
                    )
                ],
            }
        )
    report = {
        "policy": "single_observed_Words_paradigm_with_at_least_3_witnesses",
        "minimum_support": minimum_support,
        "counts": {
            "morphology_keys": len(all_keys),
            **dict(sorted(status_counts.items())),
        },
        "mappings": mappings,
    }
    return report, resolved


def whitaker_stems_by_entry(
    stems: dict[str, tuple[quantity.WhitakerStem, ...]],
) -> dict[int, dict[int, str]]:
    result: dict[int, dict[int, str]] = defaultdict(dict)
    for spelling, records in stems.items():
        for record in records:
            previous = result[record.dictionary_entry].setdefault(record.slot, spelling)
            if previous != spelling:
                raise quantity.SuggestionError(
                    f"Words entry {record.dictionary_entry} slot {record.slot} is inconsistent"
                )
    return {entry: dict(sorted(slots.items())) for entry, slots in result.items()}


def infer_stem_template(
    words_stems: dict[int, str], external_stems: tuple[tuple[int, str], ...]
) -> tuple[tuple[int, int], ...] | None:
    external_by_spelling: dict[str, list[int]] = defaultdict(list)
    for number, spelling in external_stems:
        external_by_spelling[spelling].append(number)
    template: list[tuple[int, int]] = []
    for slot, spelling in sorted(words_stems.items()):
        numbers = external_by_spelling.get(spelling, [])
        if len(numbers) != 1:
            return None
        template.append((slot, numbers[0]))
    return tuple(template) if template else None


def build_stem_template_crosswalk(
    observations: dict[str, Counter[tuple[tuple[int, int], ...]]],
    *,
    minimum_support: int = 3,
) -> tuple[dict[str, Any], dict[str, tuple[tuple[int, int], ...]]]:
    mappings: list[dict[str, Any]] = []
    resolved: dict[str, tuple[tuple[int, int], ...]] = {}
    status_counts: Counter[str] = Counter()
    for morphology_key in sorted(observations):
        counts = observations[morphology_key]
        supported = sum(counts.values())
        if len(counts) == 1 and supported >= minimum_support:
            status = "exact_empirical"
            resolved[morphology_key] = next(iter(counts))
        elif len(counts) == 1:
            status = "insufficient_support"
        else:
            status = "one_to_many"
        status_counts[status] += 1
        mappings.append(
            {
                "morphology_key": morphology_key,
                "status": status,
                "covered_witnesses": supported,
                "observed_templates": [
                    {
                        "slots": [
                            {"words_slot": slot, "external_radical": radical}
                            for slot, radical in template
                        ],
                        "witnesses": count,
                    }
                    for template, count in sorted(
                        counts.items(), key=lambda item: (-item[1], item[0])
                    )
                ],
            }
        )
    return (
        {
            "policy": "single_complete_slot_template_with_at_least_3_witnesses",
            "minimum_support": minimum_support,
            "counts": {
                "morphology_keys": len(observations),
                **dict(sorted(status_counts.items())),
            },
            "mappings": mappings,
        },
        resolved,
    )


def read_inflection_endings(
    path: Path,
) -> dict[tuple[str, int, int, int], frozenset[str]]:
    data = path.read_bytes()
    if len(data) % INFLECTION_RECORD_SIZE != 0:
        raise quantity.SuggestionError("INFLECTS.SEC size is not a multiple of 40")
    endings: dict[tuple[str, int, int, int], set[str]] = defaultdict(set)
    for offset in range(0, len(data), INFLECTION_RECORD_SIZE):
        record = data[offset : offset + INFLECTION_RECORD_SIZE]
        part_number = record[0]
        if part_number == 0:
            continue
        part = quantity.PART_NAMES.get(part_number, "OTHER")
        declension = int.from_bytes(record[4:8], "little")
        variant = int.from_bytes(record[8:12], "little")
        stem_key = int.from_bytes(record[20:24], "little")
        ending_size = int.from_bytes(record[24:28], "little")
        if stem_key not in range(1, 5) or ending_size > 7:
            raise quantity.SuggestionError("INFLECTS.SEC has an invalid rule")
        try:
            ending = record[28 : 28 + ending_size].decode("ascii")
        except UnicodeDecodeError as error:
            raise quantity.SuggestionError("INFLECTS.SEC ending is not ASCII") from error
        endings[(part, declension, variant, stem_key)].add(ending.lower())
    return {key: frozenset(values) for key, values in endings.items()}


def compatible_endings(
    rules: dict[tuple[str, int, int, int], frozenset[str]],
    paradigm: dict[str, Any],
    slot: int,
) -> frozenset[str]:
    part = paradigm["part_of_speech"]
    declension = paradigm["declension_or_conjugation"]
    variant = paradigm["variant"]
    result: set[str] = set()
    for (rule_part, rule_declension, rule_variant, rule_slot), endings in rules.items():
        if rule_part != part or rule_slot != slot:
            continue
        generic = rule_declension == 0 and rule_variant == 0
        exact = rule_declension == declension and rule_variant in {0, variant}
        if generic or exact:
            result.update(endings)
    return frozenset(result)


def read_latin_german_forms(
    database: Path, source_entry_ids: set[str]
) -> dict[str, frozenset[str]]:
    forms: dict[str, set[str]] = defaultdict(set)
    connection = sqlite3.connect(quantity.sqlite_uri(database), uri=True)
    try:
        identifiers = sorted(source_entry_ids)
        for begin in range(0, len(identifiers), 800):
            chunk = identifiers[begin : begin + 800]
            placeholders = ",".join("?" for _ in chunk)
            query = f"select vok_id, form from FORM where vok_id in ({placeholders})"
            for identifier, source_form in connection.execute(query, chunk):
                for token in re.findall(r"[^\W\d_]+", str(source_form), re.UNICODE):
                    normalized = quantity.latin_ascii(token)
                    if normalized is not None:
                        forms[str(identifier)].add(normalized)
    finally:
        connection.close()
    return {key: frozenset(values) for key, values in forms.items()}


def validate_generated_stems(
    candidates: list[dict[str, Any]],
    inflections: Path | None,
    latin_german_database: Path | None,
) -> Counter[str]:
    if inflections is None or latin_german_database is None:
        for candidate in candidates:
            candidate["latin_german_form_validation"] = {"status": "unavailable"}
        return Counter({"not_requested": len(candidates)})
    rules = read_inflection_endings(inflections)
    identifiers = {
        witness["source_entry_id"]
        for candidate in candidates
        if candidate["proposed_words_stems"] is not None
        and candidate["proposed_words_paradigm"] is not None
        for witness in candidate["witnesses"]
        if witness["source"] == "latin-german-morphological-lexicon"
    }
    forms = read_latin_german_forms(latin_german_database, identifiers)
    counts: Counter[str] = Counter()
    for candidate in candidates:
        stems = candidate["proposed_words_stems"]
        paradigm = candidate["proposed_words_paradigm"]
        identifiers_for_candidate = {
            witness["source_entry_id"]
            for witness in candidate["witnesses"]
            if witness["source"] == "latin-german-morphological-lexicon"
        }
        if stems is None or paradigm is None or not identifiers_for_candidate:
            status = "unavailable"
            candidate["latin_german_form_validation"] = {"status": status}
            counts[status] += 1
            continue
        witness_validations: list[dict[str, Any]] = []
        witness_statuses: set[str] = set()
        for identifier in sorted(identifiers_for_candidate):
            observed_forms = forms.get(identifier, frozenset())
            slots: list[dict[str, Any]] = []
            for stem_record in stems:
                slot = stem_record["slot"]
                stem = stem_record["stem"]
                endings = compatible_endings(rules, paradigm, slot)
                matched = sorted(
                    form
                    for form in observed_forms
                    if any(form == stem + ending for ending in endings)
                )
                slots.append(
                    {
                        "slot": slot,
                        "stem": stem,
                        "attested": bool(matched),
                        "matching_forms": matched[:12],
                    }
                )
            attested = sum(slot["attested"] for slot in slots)
            if slots and attested == len(slots):
                witness_status = "all_slots_attested"
            elif attested:
                witness_status = "partial"
            else:
                witness_status = "none"
            witness_statuses.add(witness_status)
            witness_validations.append(
                {
                    "source_entry_id": identifier,
                    "status": witness_status,
                    "slots": slots,
                }
            )
        if "all_slots_attested" in witness_statuses:
            status = "all_slots_attested"
        elif "partial" in witness_statuses:
            status = "partial"
        else:
            status = "none"
        candidate["latin_german_form_validation"] = {
            "status": status,
            "witnesses": witness_validations,
        }
        counts[status] += 1
    return counts


def attach_structural_drafts(candidates: list[dict[str, Any]]) -> Counter[str]:
    counts: Counter[str] = Counter()
    for candidate in candidates:
        strict_lexical = (
            not candidate["proper"]
            and candidate["support"] == "corroborated_independent"
            and len(candidate["typed_independent_families"]) >= 2
        )
        structure_ready = (
            candidate["proposed_words_paradigm"] is not None
            and candidate["proposed_words_stems"] is not None
            and candidate["stem_readiness"] == "generated_single"
        )
        gender_ready = (
            candidate["part_of_speech"] != "NOUN"
            or len(candidate["genders"]) == 1
        )
        form_status = candidate["latin_german_form_validation"]["status"]
        form_compatible = form_status in {"all_slots_attested", "unavailable"}
        if (
            not strict_lexical
            or not structure_ready
            or not gender_ready
            or not form_compatible
        ):
            candidate["structural_draft_status"] = "not_ready"
            counts["not_ready"] += 1
            continue
        status = (
            "cross_validated"
            if form_status == "all_slots_attested"
            else "empirical_structure"
        )
        candidate["structural_draft_status"] = status
        counts[status] += 1
        lexical: dict[str, Any] = {
            "part_of_speech": candidate["part_of_speech"],
            "paradigm": candidate["proposed_words_paradigm"],
            "stems": candidate["proposed_words_stems"],
        }
        if candidate["part_of_speech"] == "NOUN":
            lexical["gender"] = candidate["genders"][0]
        candidate["structural_draft"] = {
            "schema": "whitakers-words.lexeme-structural-draft.v1",
            "ascii_lemma": candidate["ascii_lemma"],
            "marked_lemmas": candidate["marked_lemmas"],
            "lexical": lexical,
            "readiness": status,
            "unresolved_fields": [
                "sense_identity",
                "canonical_meaning",
                "editorial_metadata",
                "quantity_mask",
            ],
            "source_families": candidate["independent_families"],
            "source_witnesses": candidate["witnesses"],
            "validation": candidate["latin_german_form_validation"],
        }
    return counts


def audit(
    dictionary: Path,
    entries: Iterable[quantity.DictionaryEntry],
    *,
    maximum_ending: int = 6,
    inflections: Path | None = None,
    latin_german_database: Path | None = None,
) -> tuple[dict[str, Any], tuple[dict[str, Any], ...]]:
    stems = quantity.read_whitaker_stems(dictionary)
    stems_by_entry = whitaker_stems_by_entry(stems)
    known, unknown, raw_counts = normalize_entries(entries)
    grouped_entries, unresolved = attach_unambiguous_unknown_pos(known, unknown)
    groups: dict[tuple[str, str, bool], list[NormalizedEntry]] = defaultdict(list)
    for item in grouped_entries:
        groups[item.group_key()].append(item)

    status_counts: Counter[str] = Counter()
    support_counts: Counter[str] = Counter()
    by_part: dict[str, Counter[str]] = defaultdict(Counter)
    by_lexical_kind: dict[str, Counter[str]] = defaultdict(Counter)
    source_stats: dict[str, Counter[str]] = defaultdict(Counter)
    family_intersections: Counter[str] = Counter()
    priority_counts: Counter[str] = Counter()
    crosswalk_observations: dict[
        str, Counter[tuple[str, int, int]]
    ] = defaultdict(Counter)
    crosswalk_ambiguous: Counter[str] = Counter()
    crosswalk_unmatched: Counter[str] = Counter()
    stem_template_observations: dict[
        str, Counter[tuple[tuple[int, int], ...]]
    ] = defaultdict(Counter)
    candidates: list[dict[str, Any]] = []

    for key in sorted(groups):
        ascii_lemma, part, proper = key
        group = groups[key]
        targets: dict[tuple[int, int], quantity.WhitakerStem] = {}
        for item in group:
            item_matches = find_matches(stems, item, maximum_ending=maximum_ending)
            for stem in item_matches:
                targets[(stem.dictionary_entry, stem.slot)] = stem
            morphology_key = item.dictionary.morphology_key
            if morphology_key is None or item.inferred_part:
                continue
            paradigms = {
                (stem.part_of_speech, stem.declension, stem.variant)
                for stem in item_matches
            }
            if len(paradigms) == 1:
                crosswalk_observations[morphology_key][next(iter(paradigms))] += 1
            elif paradigms:
                crosswalk_ambiguous[morphology_key] += 1
            else:
                crosswalk_unmatched[morphology_key] += 1
            entry_ids = {stem.dictionary_entry for stem in item_matches}
            if len(entry_ids) == 1 and item.dictionary.external_stems:
                template = infer_stem_template(
                    stems_by_entry[next(iter(entry_ids))],
                    item.dictionary.external_stems,
                )
                if template is not None:
                    stem_template_observations[morphology_key][template] += 1

        status = "covered" if targets else "structurally_unmatched"
        status_counts[status] += 1
        by_part[part][status] += 1
        lexical_kind = "proper" if proper else "common"
        by_lexical_kind[lexical_kind][status] += 1
        families = sorted({item.dictionary.source_family for item in group})
        independent_families = sorted(
            {
                item.dictionary.source_family
                for item in group
                if item.dictionary.independent_quantity_authority
            }
        )
        typed_independent_families = sorted(
            {
                item.dictionary.source_family
                for item in group
                if item.dictionary.independent_quantity_authority
                and not item.inferred_part
            }
        )
        derived_families = sorted(set(families) - set(independent_families))
        for item in group:
            source_stats[source_id(item.dictionary)]["group_witnesses"] += 1
            source_stats[source_id(item.dictionary)][status] += 1

        if status == "covered":
            continue
        if len(independent_families) >= 2:
            support = "corroborated_independent"
        elif independent_families:
            support = "single_independent"
        else:
            support = "derived_only"
        support_counts[support] += 1
        by_part[part][support] += 1
        by_lexical_kind[lexical_kind][support] += 1
        family_intersections["+".join(families)] += 1
        morphological_witness = any(
            item.dictionary.morphology_hint is not None for item in group
        )
        if support == "corroborated_independent":
            priority_counts["corroborated_independent"] += 1
            priority_counts[
                "corroborated_independent_proper"
                if proper
                else "corroborated_independent_common"
            ] += 1
            if not proper and morphological_witness:
                priority_counts[
                    "corroborated_common_with_morphological_lexicon_witness"
                ] += 1
            if not proper and len(typed_independent_families) >= 2:
                priority_counts[
                    "corroborated_common_with_typed_independent_pos"
                ] += 1
                if morphological_witness:
                    priority_counts[
                        "corroborated_common_typed_pos_with_morphological_witness"
                    ] += 1
        witnesses = sorted(
            (witness_record(item) for item in group),
            key=lambda record: (record["source"], record["source_entry_id"]),
        )
        candidates.append(
            {
                "schema": "whitakers-words.lexical-expansion-candidate.v1",
                "ascii_lemma": ascii_lemma,
                "part_of_speech": part,
                "proper": proper,
                "support": support,
                "independent_family_count": len(independent_families),
                "independent_families": independent_families,
                "typed_independent_families": typed_independent_families,
                "derived_families": derived_families,
                "morphological_lexicon_witness": morphological_witness,
                "genders": sorted(
                    {
                        item.dictionary.gender
                        for item in group
                        if item.dictionary.gender is not None
                    }
                ),
                "marked_lemmas": sorted(
                    {
                        item.word.marked_prefix(len(item.word.source))
                        for item in group
                        if any(mark is not None for mark in item.word.marks)
                    }
                ),
                "witness_count": len(witnesses),
                "witnesses": witnesses,
            }
        )

    crosswalk, resolved_morphology = build_morphology_crosswalk(
        crosswalk_observations, crosswalk_ambiguous, crosswalk_unmatched
    )
    stem_crosswalk, resolved_stem_templates = build_stem_template_crosswalk(
        stem_template_observations
    )
    paradigm_readiness: Counter[str] = Counter()
    stem_readiness: Counter[str] = Counter()
    for candidate in candidates:
        morphology_keys = sorted(
            {
                witness["morphology_key"]
                for witness in candidate["witnesses"]
                if "morphology_key" in witness
            }
        )
        mapped = {
            key: resolved_morphology[key]
            for key in morphology_keys
            if key in resolved_morphology
        }
        paradigms = set(mapped.values())
        systems = {key.partition(":")[0] for key in mapped}
        if len(paradigms) > 1:
            readiness = "mapped_conflict"
        elif not paradigms:
            readiness = "unmapped"
        elif len(systems) >= 2:
            readiness = "mapped_system_agreement"
        else:
            readiness = "mapped_single_system"
        candidate["paradigm_readiness"] = readiness
        candidate["mapped_morphology_keys"] = sorted(mapped)
        candidate["unmapped_morphology_keys"] = sorted(set(morphology_keys) - set(mapped))
        candidate["proposed_words_paradigm"] = (
            paradigm_record(next(iter(paradigms))) if len(paradigms) == 1 else None
        )
        stem_proposals: set[tuple[tuple[int, str], ...]] = set()
        mapped_template_keys: set[str] = set()
        for witness in candidate["witnesses"]:
            key = witness.get("morphology_key")
            if key not in resolved_stem_templates or "external_stems" not in witness:
                continue
            external = {
                record["number"]: record["stem"]
                for record in witness["external_stems"]
            }
            template = resolved_stem_templates[key]
            if not all(radical in external for _, radical in template):
                continue
            stem_proposals.add(
                tuple((slot, external[radical]) for slot, radical in template)
            )
            mapped_template_keys.add(key)
        if len(stem_proposals) > 1:
            stem_status = "generated_conflict"
        elif stem_proposals:
            stem_status = "generated_single"
        else:
            stem_status = "unmapped"
        candidate["stem_readiness"] = stem_status
        candidate["mapped_stem_template_keys"] = sorted(mapped_template_keys)
        candidate["proposed_words_stems"] = (
            [
                {"slot": slot, "stem": stem}
                for slot, stem in next(iter(stem_proposals))
            ]
            if len(stem_proposals) == 1
            else None
        )
        paradigm_readiness[readiness] += 1
        stem_readiness[stem_status] += 1
        strict_lexical = (
            not candidate["proper"]
            and candidate["support"] == "corroborated_independent"
            and len(candidate["typed_independent_families"]) >= 2
        )
        if strict_lexical and readiness in {
            "mapped_single_system",
            "mapped_system_agreement",
        }:
            paradigm_readiness["strict_lexical_with_empirical_paradigm"] += 1
            if stem_status == "generated_single":
                stem_readiness[
                    "strict_lexical_with_empirical_paradigm_and_stems"
                ] += 1

    unresolved_matches = sum(
        bool(find_matches(stems, item, maximum_ending=maximum_ending))
        for item in unresolved
    )
    form_validation = validate_generated_stems(
        candidates, inflections, latin_german_database
    )
    structural_drafts = attach_structural_drafts(candidates)
    dictionary_size = dictionary.stat().st_size
    report = {
        "schema": "whitakers-words.lexical-expansion-audit.v1",
        "policy": {
            "unit": "normalized_ascii_headword_part_of_speech_group",
            "coverage": "compatible_Words_slot_1_prefix_and_citation_ending",
            "new_lexeme_claim": False,
            "note": (
                "Structurally unmatched groups are import candidates, not proof of "
                "distinct lexemes; paradigms, senses, and variants still require review."
            ),
        },
        "words": {
            "dictionary_records": dictionary_size // quantity.DICTIONARY_RECORD_SIZE,
            "indexed_stem_spellings": len(stems),
            "indexed_stem_records": sum(len(items) for items in stems.values()),
        },
        "input": {
            **dict(sorted(raw_counts.items())),
            "unknown_pos_attached_unambiguously": sum(
                item.inferred_part for item in grouped_entries
            ),
            "unknown_pos_unresolved": len(unresolved),
            "unresolved_unknown_pos_with_weak_words_match": unresolved_matches,
        },
        "groups": {
            "total_typed": len(groups),
            **dict(sorted(status_counts.items())),
            "unmatched_support": dict(sorted(support_counts.items())),
            "priority_queue": dict(sorted(priority_counts.items())),
            "paradigm_readiness": dict(sorted(paradigm_readiness.items())),
            "stem_readiness": dict(sorted(stem_readiness.items())),
            "latin_german_form_validation": dict(sorted(form_validation.items())),
            "structural_drafts": dict(sorted(structural_drafts.items())),
        },
        "by_part_of_speech": {
            part: dict(sorted(counts.items()))
            for part, counts in sorted(by_part.items())
        },
        "by_lexical_kind": {
            kind: dict(sorted(counts.items()))
            for kind, counts in sorted(by_lexical_kind.items())
        },
        "unmatched_family_intersections": dict(
            sorted(family_intersections.items(), key=lambda item: (-item[1], item[0]))
        ),
        "sources": {
            source: dict(sorted(counts.items()))
            for source, counts in sorted(source_stats.items())
        },
        "morphology_crosswalk": crosswalk,
        "stem_template_crosswalk": stem_crosswalk,
    }
    candidates.sort(
        key=lambda item: (
            {"corroborated_independent": 0, "single_independent": 1, "derived_only": 2}[
                item["support"]
            ],
            item["ascii_lemma"],
            item["part_of_speech"],
            item["proper"],
        )
    )
    return report, tuple(candidates)


def render_jsonl(records: Iterable[dict[str, Any]]) -> str:
    return "".join(
        json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n"
        for record in records
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dictionary", type=Path, help="Whitaker DICTFILE.GEN")
    parser.add_argument("superdb", type=Path, help="read-only unified dictionary index")
    parser.add_argument(
        "--source",
        action="append",
        choices=sorted(quantity.SQLITE_SOURCE_NAMES),
        dest="sources",
    )
    parser.add_argument("--collatinus-data", type=Path)
    parser.add_argument("--collatinus-extended", action="store_true")
    parser.add_argument("--latin-german", type=Path)
    parser.add_argument(
        "--faria-v3",
        type=Path,
        help="read-only Faria v3 quality database; publishable lexical entries only",
    )
    parser.add_argument("--maximum-ending", type=int, default=6)
    parser.add_argument(
        "--output", type=Path, required=True, help="unmatched JSONL groups"
    )
    parser.add_argument("--report", type=Path, required=True, help="summary JSON")
    parser.add_argument(
        "--draft-output",
        type=Path,
        help="optional JSONL containing only structurally complete lexeme drafts",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.maximum_ending < 0 or arguments.maximum_ending > quantity.STEM_SIZE:
        raise quantity.SuggestionError("--maximum-ending must be in 0..18")
    if arguments.collatinus_extended and arguments.collatinus_data is None:
        raise quantity.SuggestionError("--collatinus-extended requires --collatinus-data")

    sources = arguments.sources or sorted(quantity.SQLITE_SOURCE_NAMES)
    entries: Iterable[quantity.DictionaryEntry] = quantity.read_dictionary_entries(
        arguments.superdb, sources
    )
    if arguments.collatinus_data is not None:
        entries = itertools.chain(
            entries,
            quantity.read_collatinus_entries(
                arguments.collatinus_data,
                include_extended=arguments.collatinus_extended,
            ),
        )
    if arguments.latin_german is not None:
        entries = itertools.chain(
            entries, quantity.read_latin_german_entries(arguments.latin_german)
        )
    if arguments.faria_v3 is not None:
        entries = itertools.chain(entries, read_faria_v3_entries(arguments.faria_v3))

    report, candidates = audit(
        arguments.dictionary,
        entries,
        maximum_ending=arguments.maximum_ending,
        inflections=arguments.dictionary.parent / "INFLECTS.SEC",
        latin_german_database=arguments.latin_german,
    )
    arguments.output.write_text(render_jsonl(candidates), encoding="utf-8")
    if arguments.draft_output is not None:
        arguments.draft_output.write_text(
            render_jsonl(
                candidate["structural_draft"]
                for candidate in candidates
                if "structural_draft" in candidate
            ),
            encoding="utf-8",
        )
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"wrote {len(candidates)} structurally unmatched groups to {arguments.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
