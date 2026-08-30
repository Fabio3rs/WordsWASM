#!/usr/bin/env python3

"""Turn generated structural lexeme drafts into a deterministic review queue."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable, Iterator


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

import suggest_quantity_evidence as quantity


INPUT_SCHEMA = "whitakers-words.lexeme-structural-draft.v1"
OUTPUT_SCHEMA = "whitakers-words.lexeme-review-candidate.v1"
LONG = "long"
SHORT = "short"
MARK_NAMES = {quantity.MACRON: LONG, quantity.BREVE: SHORT}
VOWELS = frozenset("aeiouy")
SOURCE_LANGUAGES = {
    "collatinus-derived-lexicon": "en",
    "faria-v3-quality": "pt",
    "gaffiot": "fr",
    "latin-german-morphological-lexicon": "de",
    "lewis-short-ls-dict": "en",
}
RUNTIME_PARTS = {
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


class ReviewError(ValueError):
    """A structural draft cannot safely enter the editorial queue."""


def canonical_json(value: Any) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def revision_of(value: Any) -> str:
    # WHY: decisions must pin the exact generated evidence they reviewed. A
    # content digest makes a changed dictionary snapshot invalidate approval
    # instead of silently reusing it for a different candidate.
    return "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ReviewError(message)


def validate_draft(record: Any, context: str) -> dict[str, Any]:
    require(isinstance(record, dict), f"{context}: draft must be an object")
    require(record.get("schema") == INPUT_SCHEMA, f"{context}: unsupported schema")
    lemma = record.get("ascii_lemma")
    require(
        isinstance(lemma, str) and quantity.LATIN_TOKEN.fullmatch(lemma) is not None,
        f"{context}: ascii_lemma must contain lowercase ASCII letters",
    )
    lexical = record.get("lexical")
    require(isinstance(lexical, dict), f"{context}: lexical must be an object")
    part = lexical.get("part_of_speech")
    require(part in RUNTIME_PARTS, f"{context}: unsupported part_of_speech")
    paradigm = lexical.get("paradigm")
    require(isinstance(paradigm, dict), f"{context}: missing paradigm")
    require(
        paradigm.get("part_of_speech") == part,
        f"{context}: paradigm POS differs from lexical POS",
    )
    stems = lexical.get("stems")
    require(isinstance(stems, list) and stems, f"{context}: stems must be non-empty")
    slots: set[int] = set()
    for index, stem in enumerate(stems):
        stem_context = f"{context}: stems[{index}]"
        require(isinstance(stem, dict), f"{stem_context}: must be an object")
        slot = stem.get("slot")
        value = stem.get("stem")
        require(
            isinstance(slot, int) and 1 <= slot <= 4 and slot not in slots,
            f"{stem_context}: slot must be unique and in 1..4",
        )
        require(
            isinstance(value, str)
            and 1 <= len(value) <= quantity.STEM_SIZE
            and quantity.LATIN_TOKEN.fullmatch(value) is not None,
            f"{stem_context}: stem must be 1..18 lowercase ASCII letters",
        )
        slots.add(slot)
    marked_lemmas = record.get("marked_lemmas")
    require(isinstance(marked_lemmas, list), f"{context}: marked_lemmas must be an array")
    for index, marked_lemma in enumerate(marked_lemmas):
        marked_context = f"{context}: marked_lemmas[{index}]"
        require(isinstance(marked_lemma, str), f"{marked_context}: must be a string")
        require(
            unicodedata.normalize("NFC", marked_lemma) == marked_lemma,
            f"{marked_context}: must be NFC",
        )
        word = quantity.extract_first_word(marked_lemma, require_quantity=False)
        require(
            word is not None and word.source == lemma,
            f"{marked_context}: does not normalize to {lemma!r}",
        )
    witnesses = record.get("source_witnesses")
    require(
        isinstance(witnesses, list) and witnesses,
        f"{context}: source_witnesses must be a non-empty array",
    )
    witness_keys: set[tuple[str, str]] = set()
    family_authority: dict[str, bool] = {}
    for index, witness in enumerate(witnesses):
        witness_context = f"{context}: source_witnesses[{index}]"
        require(isinstance(witness, dict), f"{witness_context}: must be an object")
        for key in ("source", "source_family", "source_entry_id", "lemma", "head"):
            require(
                isinstance(witness.get(key), str) and witness[key],
                f"{witness_context}: missing {key}",
            )
        require(
            isinstance(witness.get("independent_authority"), bool),
            f"{witness_context}: independent_authority must be boolean",
        )
        key = (witness["source"], witness["source_entry_id"])
        require(key not in witness_keys, f"{witness_context}: duplicate witness")
        witness_keys.add(key)
        family = witness["source_family"]
        authority = witness["independent_authority"]
        require(
            family_authority.get(family, authority) == authority,
            f"{witness_context}: inconsistent family authority",
        )
        family_authority[family] = authority
        word = quantity.extract_first_word(witness["lemma"], require_quantity=False)
        require(word is not None, f"{witness_context}: unsupported lemma spelling")
        require(
            word.source == lemma,
            f"{witness_context}: lemma does not normalize to {lemma!r}",
        )
    independent_families = sorted(
        family for family, independent in family_authority.items() if independent
    )
    require(
        len(independent_families) >= 2,
        f"{context}: review queue requires two independent families",
    )
    require(
        record.get("source_families") == independent_families,
        f"{context}: source_families differs from independent witnesses",
    )
    require(
        record.get("readiness") in {"cross_validated", "empirical_structure"},
        f"{context}: unsupported readiness",
    )
    require(isinstance(record.get("validation"), dict), f"{context}: missing validation")
    unresolved = record.get("unresolved_fields")
    require(
        isinstance(unresolved, list)
        and all(isinstance(item, str) and item for item in unresolved),
        f"{context}: unresolved_fields must contain strings",
    )
    return record


def read_jsonl(path: Path) -> Iterator[dict[str, Any]]:
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise ReviewError(f"{path}:{line_number}: invalid JSON") from error
        yield validate_draft(value, f"{path}:{line_number}")


def read_analysis_jsonl(path: Path) -> dict[str, dict[str, Any]]:
    analyses: dict[str, dict[str, Any]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        context = f"{path}:{line_number}"
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ReviewError(f"{context}: invalid JSON") from error
        require(isinstance(record, dict), f"{context}: analysis must be an object")
        require(
            record.get("schema") == "whitakers-words.analysis"
            and record.get("schemaVersion") == 1,
            f"{context}: expected whitakers-words.analysis v1",
        )
        query = record.get("query")
        normalized = query.get("normalized") if isinstance(query, dict) else None
        require(
            isinstance(normalized, str)
            and quantity.LATIN_TOKEN.fullmatch(normalized) is not None,
            f"{context}: query.normalized must be lowercase Latin ASCII",
        )
        require(normalized not in analyses, f"{context}: duplicate query {normalized!r}")
        require(
            record.get("status") in {"analyzed", "unknown"},
            f"{context}: baseline query must be analyzed or unknown",
        )
        require(isinstance(record.get("analyses"), list), f"{context}: missing analyses")
        analyses[normalized] = record
    return analyses


def summarize_baseline(record: dict[str, Any], target_part: str) -> dict[str, Any]:
    methods = sorted(
        {
            item.get("derivation", {}).get("method")
            for item in record["analyses"]
            if isinstance(item, dict)
            and isinstance(item.get("derivation"), dict)
            and isinstance(item["derivation"].get("method"), str)
        }
    )
    suggestions = record.get("suggestions", [])
    has_two_words = any(
        isinstance(item, dict) and item.get("method") == "two-words"
        for item in suggestions
    )
    if record["status"] == "unknown":
        coverage = "unknown_with_suggestion" if has_two_words else "unknown"
    elif set(methods) & {"regular", "unique"}:
        coverage = "direct"
    elif methods and set(methods) <= {"derived"}:
        coverage = "artificial"
    elif methods and set(methods) <= {"orthographic", "syncope"}:
        coverage = "repaired"
    else:
        coverage = "mixed"

    lexemes: dict[tuple[str, int | None], dict[str, Any]] = {}
    expected_part = RUNTIME_PARTS[target_part]
    matching_form_part = False
    for analysis in record["analyses"]:
        require(isinstance(analysis, dict), "baseline analysis item must be an object")
        if analysis.get("partOfSpeech") == expected_part:
            matching_form_part = True
        lexeme = analysis.get("lexeme")
        require(isinstance(lexeme, dict), "baseline analysis lacks lexeme")
        dictionary = lexeme.get("dictionary")
        entry_id = lexeme.get("entryId")
        require(isinstance(dictionary, str), "baseline lexeme lacks dictionary")
        require(entry_id is None or isinstance(entry_id, int), "invalid baseline entryId")
        key = (dictionary, entry_id)
        item = lexemes.setdefault(
            key,
            {
                "dictionary": dictionary,
                "entry_id": entry_id,
                "dictionary_form": lexeme.get("dictionaryForm"),
                "part_of_speech": lexeme.get("partOfSpeech"),
                "meaning": lexeme.get("meaning"),
                "derivation_methods": set(),
            },
        )
        method = analysis.get("derivation", {}).get("method")
        if isinstance(method, str):
            item["derivation_methods"].add(method)
    serialized_lexemes = []
    for item in lexemes.values():
        item["derivation_methods"] = sorted(item["derivation_methods"])
        serialized_lexemes.append(item)
    serialized_lexemes.sort(
        key=lambda item: (item["dictionary"], item["entry_id"] or 0)
    )

    result: dict[str, Any] = {
        "status": record["status"],
        "coverage": coverage,
        "analysis_count": len(record["analyses"]),
        "derivation_methods": methods,
        "target_part_of_speech_present": matching_form_part,
        "lexemes": serialized_lexemes,
    }
    if has_two_words:
        result["two_words_suggestions"] = [
            {
                "split_at": item["splitAt"],
                "segments": [segment["text"] for segment in item["segments"]],
            }
            for item in suggestions
            if isinstance(item, dict) and item.get("method") == "two-words"
        ]
    return result


def witness_quantity_votes(
    lemma: str, witnesses: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], str, list[str]]:
    by_position: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for witness in witnesses:
        word = quantity.extract_first_word(witness["lemma"], require_quantity=False)
        if word is None:
            continue
        for position, mark in enumerate(word.marks):
            if mark is None:
                continue
            by_position[position].append(
                {
                    "source_ref": (
                        f'{witness["source"]}:{witness["source_entry_id"]}'
                    ),
                    "source_family": witness["source_family"],
                    "independent_authority": witness["independent_authority"],
                    "quantity": MARK_NAMES[mark],
                }
            )

    positions: list[dict[str, Any]] = []
    flags: set[str] = set()
    for position, letter in enumerate(lemma):
        if letter not in VOWELS:
            continue
        votes = sorted(
            by_position.get(position, []),
            key=lambda item: (item["source_family"], item["source_ref"]),
        )
        independent_family_marks: dict[str, set[str]] = defaultdict(set)
        independent_families: dict[str, set[str]] = defaultdict(set)
        derived_marks: set[str] = set()
        for vote in votes:
            if vote["independent_authority"]:
                independent_family_marks[vote["source_family"]].add(
                    vote["quantity"]
                )
                independent_families[vote["quantity"]].add(vote["source_family"])
            else:
                derived_marks.add(vote["quantity"])

        internal_conflict = any(
            len(marks) > 1 for marks in independent_family_marks.values()
        )
        independent_marks = {
            mark for mark, families in independent_families.items() if families
        }
        consensus: str | None = None
        if internal_conflict or len(independent_marks) > 1:
            status = "conflict"
            flags.add("independent_quantity_conflict")
        elif independent_marks:
            mark = next(iter(independent_marks))
            support = len(independent_families[mark])
            if support >= 2:
                status = "consensus"
                consensus = mark
                if derived_marks - {mark}:
                    flags.add("derived_quantity_disagreement")
            elif derived_marks - {mark}:
                # WHY: one independent witness cannot settle a disagreement
                # with a derived compilation. Keep the quantity unknown until
                # another independent authority or an editor resolves it.
                status = "conflict"
                flags.add("derived_quantity_disagreement")
            else:
                status = "single_source"
        elif len(derived_marks) == 1:
            status = "derived_only"
        elif len(derived_marks) > 1:
            status = "derived_conflict"
            flags.add("derived_quantity_conflict")
        else:
            status = "unknown"

        item: dict[str, Any] = {
            "letter_index": position,
            "letter": letter,
            "status": status,
            "votes": votes,
        }
        if consensus is not None:
            item["consensus"] = consensus
        positions.append(item)

    statuses = {item["status"] for item in positions}
    if "conflict" in statuses:
        overall = "conflict"
    elif positions and statuses == {"consensus"}:
        overall = "consensus_complete"
    elif "consensus" in statuses:
        overall = "consensus_partial"
    elif "single_source" in statuses:
        overall = "single_source_only"
    elif statuses & {"derived_only", "derived_conflict"}:
        overall = "derived_only"
    else:
        overall = "unknown"
    return positions, overall, sorted(flags)


def reference_only(witness: dict[str, Any]) -> bool:
    # WHY: this classifier only prioritizes obvious remissions for an editor;
    # it deliberately avoids interpreting a later "cf." as absence of a gloss.
    normalized = unicodedata.normalize("NFKD", witness["head"].lower())
    text = "".join(character for character in normalized if ord(character) < 128)
    source = witness["source"]
    patterns = {
        "lewis-short-ls-dict": r"\b(?:adj|adv|n|pron)\.,?\s*(?:v\.|see\b)",
        "gaffiot": r"[:,]\s*(?:v\.|voir\b)\s+[a-z]",
        "latin-german-morphological-lexicon": r";\s*(?:s\.|siehe\b|vgl\.)\s+[a-z]",
        "faria-v3-quality": r"[:,;]\s*(?:v\.|veja\b)\s+[a-z]",
    }
    pattern = patterns.get(source)
    return pattern is not None and re.search(pattern, text) is not None


def semantic_evidence(
    witnesses: list[dict[str, Any]], quantity_flags: Iterable[str]
) -> tuple[dict[str, Any], list[str], str]:
    flags = set(quantity_flags)
    by_family: dict[str, list[dict[str, Any]]] = defaultdict(list)
    heads: list[dict[str, Any]] = []
    for witness in witnesses:
        by_family[witness["source_family"]].append(witness)
        role = "reference_only" if reference_only(witness) else "headword_and_gloss"
        if role == "reference_only":
            flags.add("reference_only_witness")
        if witness.get("part_of_speech_inferred") is True:
            flags.add("inferred_part_of_speech_witness")
        heads.append(
            {
                "source_ref": f'{witness["source"]}:{witness["source_entry_id"]}',
                "source_family": witness["source_family"],
                "independent_authority": witness["independent_authority"],
                "language": SOURCE_LANGUAGES.get(witness["source"], "und"),
                "role": role,
                "lemma": witness["lemma"],
                "text": witness["head"],
            }
        )
    multiplicity: list[dict[str, Any]] = []
    for family, family_witnesses in sorted(by_family.items()):
        identifiers = sorted({item["source_entry_id"] for item in family_witnesses})
        independent = any(item["independent_authority"] for item in family_witnesses)
        multiplicity.append(
            {
                "source_family": family,
                "independent_authority": independent,
                "entry_count": len(identifiers),
                "source_entry_ids": identifiers,
            }
        )
        if independent and len(identifiers) > 1:
            flags.add("multiple_entries_in_independent_family")

    high_risk = {
        "independent_quantity_conflict",
        "multiple_entries_in_independent_family",
    }
    medium_risk = {
        "derived_quantity_conflict",
        "derived_quantity_disagreement",
        "inferred_part_of_speech_witness",
        "reference_only_witness",
        "structure_not_cross_validated",
    }
    if flags & high_risk:
        priority = "high"
    elif flags & medium_risk:
        priority = "medium"
    else:
        priority = "normal"
    return (
        {
            # WHY: cross-language gloss overlap is not a sound sense identity
            # rule. These are source heads for a human decision, not proposed
            # canonical meanings and never pass directly into the runtime DB.
            "status": "manual_alignment_required",
            "family_multiplicity": multiplicity,
            "headword_evidence": sorted(
                heads, key=lambda item: (item["source_family"], item["source_ref"])
            ),
        },
        sorted(flags),
        priority,
    )


def prepare_candidate(
    draft: dict[str, Any], baseline: dict[str, Any] | None = None
) -> dict[str, Any]:
    draft = validate_draft(draft, "draft")
    lemma = draft["ascii_lemma"]
    lexical = draft["lexical"]
    part = lexical["part_of_speech"]
    proper = False
    identity = {
        "ascii_lemma": lemma,
        "part_of_speech": part,
        "proper": proper,
    }
    draft_id = f"lexdraft:{part.lower()}:{'proper' if proper else 'common'}:{lemma}"
    revision = revision_of(draft)
    witnesses = draft["source_witnesses"]
    positions, quantity_status, quantity_flags = witness_quantity_votes(
        lemma, witnesses
    )
    if draft.get("readiness") != "cross_validated":
        quantity_flags.append("structure_not_cross_validated")
    semantics, flags, priority = semantic_evidence(witnesses, quantity_flags)
    result = {
        "schema": OUTPUT_SCHEMA,
        "draft_id": draft_id,
        "revision": revision,
        "key": identity,
        "structure": {
            "readiness": draft["readiness"],
            "lexical": lexical,
            "validation": draft["validation"],
        },
        "evidence": {
            "marked_lemmas": draft["marked_lemmas"],
            "source_families": draft["source_families"],
            "quantity": {
                "status": quantity_status,
                "positions": positions,
            },
            "semantics": semantics,
        },
        "triage": {
            "priority": priority,
            "flags": flags,
        },
        "decision": {
            "status": "needs_review",
            "automatic_promotion_allowed": False,
            "required_fields": list(draft["unresolved_fields"]),
        },
    }
    if baseline is not None:
        summary = summarize_baseline(baseline, part)
        result["baseline"] = summary
        result["baseline_revision"] = revision_of(baseline)
        coverage_flag = f'baseline_{summary["coverage"]}'
        result["triage"]["flags"] = sorted(
            set(result["triage"]["flags"]) | {coverage_flag}
        )
        if summary["status"] == "analyzed" and not summary[
            "target_part_of_speech_present"
        ]:
            result["triage"]["flags"].append("baseline_different_part_of_speech")
            result["triage"]["flags"].sort()
    return result


def prepare(
    drafts: Iterable[dict[str, Any]],
    baselines: dict[str, dict[str, Any]] | None = None,
) -> tuple[dict[str, Any], ...]:
    draft_items = tuple(drafts)
    if baselines is not None:
        draft_lemmas = {draft["ascii_lemma"] for draft in draft_items}
        require(
            set(baselines) == draft_lemmas,
            "analysis baseline queries must exactly match the draft lemmas",
        )
    records = [
        prepare_candidate(
            draft,
            baselines.get(draft["ascii_lemma"]) if baselines is not None else None,
        )
        for draft in draft_items
    ]
    records.sort(key=lambda item: (item["draft_id"], item["revision"]))
    seen: set[str] = set()
    for record in records:
        require(record["draft_id"] not in seen, f'duplicate {record["draft_id"]}')
        seen.add(record["draft_id"])
    return tuple(records)


def report(records: Iterable[dict[str, Any]]) -> dict[str, Any]:
    items = tuple(records)
    priority = Counter(item["triage"]["priority"] for item in items)
    flags = Counter(flag for item in items for flag in item["triage"]["flags"])
    quantity_status = Counter(
        item["evidence"]["quantity"]["status"] for item in items
    )
    result = {
        "schema": "whitakers-words.lexeme-review-report.v1",
        "records": len(items),
        # WHY: structure and corroboration can order the queue, but neither
        # establishes that source entries denote one sense. Only a pinned
        # editorial decision may authorize a runtime lexeme.
        "automatic_promotions": 0,
        "decision_status": {"needs_review": len(items)},
        "priority": dict(sorted(priority.items())),
        "quantity_status": dict(sorted(quantity_status.items())),
        "flags": dict(sorted(flags.items())),
    }
    baseline = Counter(
        item["baseline"]["coverage"] for item in items if "baseline" in item
    )
    if baseline:
        result["baseline_coverage"] = dict(sorted(baseline.items()))
    return result


def render_jsonl(records: Iterable[dict[str, Any]]) -> str:
    return "".join(
        json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n"
        for record in records
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("drafts", type=Path, help="generated structural draft JSONL")
    parser.add_argument("--output", type=Path, required=True, help="review queue JSONL")
    parser.add_argument("--report", type=Path, required=True, help="queue summary JSON")
    parser.add_argument(
        "--analysis-input",
        type=Path,
        help="optional canonical analysis-v1 JSONL for current-engine triage",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    baselines = (
        read_analysis_jsonl(arguments.analysis_input)
        if arguments.analysis_input is not None
        else None
    )
    records = prepare(read_jsonl(arguments.drafts), baselines)
    arguments.output.write_text(render_jsonl(records), encoding="utf-8")
    arguments.report.write_text(
        json.dumps(report(records), ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {len(records)} review candidates to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
