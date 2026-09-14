#!/usr/bin/env python3

"""Build a review-only lexical roadmap from TLL CLTK lemma evidence."""

from __future__ import annotations

import argparse
import csv
import gzip
import itertools
import json
import sqlite3
import sys
import unicodedata
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Iterator

import suggest_quantity_evidence as quantity
from extract_tll_word_frequencies import (
    WORD_RE,
    atomic_text_output,
    file_digest,
    normalize_form,
    readonly_connection,
    write_json,
)
from rank_tll_unknown_words import compact_json, open_text_input


REPORT_SCHEMA = "whitakers-words.tll-cltk-roadmap-report.v1"
CANDIDATE_SCHEMA = "whitakers-words.tll-cltk-roadmap-candidate.v1"
EVIDENCE_FIELDS = (
    "form",
    "corpus_occurrences",
    "corpus_artifact_count",
    "stage_one_queue",
    "cltk_lemma",
    "cltk_lemma_norm",
    "cltk_upos",
    "pilot_token_count",
    "pilot_document_count",
    "surface_lemma_count",
    "dictionary_status",
    "dictionary_source_count",
    "pos_assessment",
    "dictionary_hits_json",
    "structural_candidates_json",
)
ROADMAP_FIELDS = (
    "queue",
    "lemma",
    "cltk_upos",
    "corpus_occurrences_upper_bound",
    "pilot_lemma_occurrences",
    "pilot_document_count",
    "contributing_form_count",
    "ambiguous_form_count",
    "dictionary_support",
    "dictionary_sources",
    "dictionary_entry_count",
    "pos_assessment",
    "also_in_structural_stage",
    "top_forms",
)


class CltkRoadmapError(RuntimeError):
    """Raised when an input cannot support a reproducible roadmap."""


@dataclass(frozen=True, slots=True)
class UnknownForm:
    form: str
    occurrences: int
    artifact_count: int
    queue: str


@dataclass(slots=True)
class PairStats:
    token_count: int = 0
    documents: set[int] = field(default_factory=set)
    raw_lemmas: Counter[str] = field(default_factory=Counter)


@dataclass(frozen=True, slots=True)
class DictionaryHit:
    source: str
    source_entry_id: str
    lemma: str
    part_of_speech: str | None
    quality: str
    needs_review: bool
    redirect_only: bool
    details: str

    def record(self, upos: str) -> dict[str, object]:
        return {
            "source": self.source,
            "source_entry_id": self.source_entry_id,
            "lemma": self.lemma,
            "part_of_speech": self.part_of_speech,
            "pos_assessment": assess_pos(upos, self.part_of_speech),
            "quality": self.quality,
            "needs_review": self.needs_review,
            "redirect_only": self.redirect_only,
            "details": self.details,
        }


def chunks(values: list[str], size: int = 400) -> Iterator[list[str]]:
    for offset in range(0, len(values), size):
        yield values[offset : offset + size]


def normalize_lemma(value: str) -> str | None:
    expanded = value.replace("æ", "ae").replace("Æ", "Ae")
    expanded = expanded.replace("œ", "oe").replace("Œ", "Oe")
    letters = "".join(
        char
        for char in unicodedata.normalize("NFD", expanded).casefold()
        if unicodedata.combining(char) == 0
    )
    return letters if letters and all("a" <= char <= "z" for char in letters) else None


def token_form(surface: str) -> str | None:
    matches = list(WORD_RE.finditer(surface))
    if len(matches) != 1:
        return None
    return normalize_form(matches[0].group(0))


def load_unknown_forms(path: Path) -> dict[str, UnknownForm]:
    result: dict[str, UnknownForm] = {}
    with open_text_input(path) as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        required = {
            "form", "occurrences", "artifact_count", "coverage_status", "queue"
        }
        if not required <= set(reader.fieldnames or ()):
            raise CltkRoadmapError("unknown-form TSV has an unsupported header")
        for row in reader:
            if row["coverage_status"] != "unknown":
                raise CltkRoadmapError(
                    "unknown-form TSV contains a non-unknown coverage status"
                )
            item = UnknownForm(
                str(row["form"]), int(row["occurrences"]),
                int(row["artifact_count"]), str(row["queue"])
            )
            if item.form in result:
                raise CltkRoadmapError("unknown-form TSV contains duplicate forms")
            result[item.form] = item
    return result


def load_jsonl(path: Path) -> Iterator[dict[str, Any]]:
    with open_text_input(path) as stream:
        for number, line in enumerate(stream, start=1):
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise CltkRoadmapError(
                    f"invalid structural candidate JSON on line {number}: {error}"
                ) from error
            if not isinstance(value, dict):
                raise CltkRoadmapError("structural candidate JSONL must contain objects")
            yield value


def load_structural_candidates(
    path: Path,
) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in load_jsonl(path):
        if row.get("schema") != "whitakers-words.tll-lexeme-priority.v1":
            raise CltkRoadmapError("structural candidates use an unsupported schema")
        key = row.get("key")
        if not isinstance(key, dict) or not key.get("ascii_lemma"):
            raise CltkRoadmapError("structural candidate is missing its key")
        record = {
            "ascii_lemma": str(key["ascii_lemma"]),
            "part_of_speech": str(key.get("part_of_speech", "")),
            "proper": bool(key.get("proper", False)),
        }
        result[record["ascii_lemma"]].append(record)
    for records in result.values():
        records.sort(key=lambda row: compact_json(row))
    return result


def validate_stage_one(
    report_path: Path, unknown_path: Path, candidates_path: Path
) -> dict[str, Any]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("schema") != "whitakers-words.tll-corpus-coverage-report.v1":
        raise CltkRoadmapError("stage-one report has an unsupported schema")
    outputs = report.get("outputs", {})
    expected = {
        "unknown": (unknown_path, outputs.get("unknown", {}).get("sha256")),
        "candidates": (
            candidates_path,
            outputs.get("candidates", {}).get("sha256"),
        ),
    }
    for name, (path, digest) in expected.items():
        if digest != file_digest(path):
            raise CltkRoadmapError(
                f"stage-one report does not describe the supplied {name} file"
            )
    return report


def cltk_run_metadata(
    connection: sqlite3.Connection, run_id: int, selection_run_id: int
) -> dict[str, object]:
    rows = connection.execute(
        "SELECT r.id,r.run_type,r.pipeline_name,r.pipeline_version,r.status,"
        "r.is_active,r.config_sha256,r.config_json,r.started_at,r.finished_at,"
        "cr.id cltk_run_id,cr.cltk_version,cr.language,"
        "cr.pipeline_name cltk_pipeline,cr.model_manifest_json "
        "FROM processing_run r JOIN cltk_run cr ON cr.processing_run_id=r.id "
        "WHERE r.id=?",
        (run_id,),
    ).fetchall()
    if len(rows) != 1:
        raise CltkRoadmapError(f"CLTK processing run {run_id} does not exist")
    row = rows[0]
    if row["run_type"] not in {"cltk", "cltk_pilot"} or row["status"] != "completed":
        raise CltkRoadmapError("CLTK run must be a completed cltk or cltk_pilot run")
    inputs = connection.execute(
        "SELECT input_run_id,role,sequence_no FROM processing_run_input "
        "WHERE processing_run_id=? ORDER BY sequence_no,input_run_id",
        (run_id,),
    ).fetchall()
    preparation = [
        int(item["input_run_id"])
        for item in inputs
        if item["role"] == "nlp_preparation"
    ]
    if preparation != [selection_run_id]:
        raise CltkRoadmapError(
            "CLTK run does not use the stage-one nlp_prepare selection"
        )
    counts = connection.execute(
        "SELECT count(DISTINCT d.id),count(t.id),"
        "count(DISTINCT CASE WHEN trim(coalesce(t.lemma,''))<>'' "
        "THEN lower(t.lemma) END),"
        "sum(t.text_unit_id IS NULL OR t.unit_char_start IS NULL "
        "OR t.unit_char_end IS NULL OR t.unit_char_end<t.unit_char_start) "
        "FROM cltk_document d "
        "JOIN cltk_sentence s ON s.cltk_document_id=d.id "
        "JOIN cltk_token t ON t.cltk_sentence_id=s.id "
        "WHERE d.cltk_run_id=?",
        (int(row["cltk_run_id"]),),
    ).fetchone()
    if int(counts[0]) == 0 or int(counts[1]) == 0 or int(counts[2]) == 0:
        raise CltkRoadmapError("CLTK run has no usable documents, tokens, or lemmas")
    if int(counts[3] or 0) != 0:
        raise CltkRoadmapError("CLTK run contains tokens with invalid unit offsets")
    result = dict(row)
    result["model_manifest"] = json.loads(str(result.pop("model_manifest_json")))
    result["config"] = json.loads(str(result.pop("config_json")))
    result["inputs"] = [dict(item) for item in inputs]
    result["counts"] = {
        "documents": int(counts[0]),
        "tokens": int(counts[1]),
        "distinct_raw_lemmas": int(counts[2]),
        "tokens_with_invalid_offsets": int(counts[3] or 0),
    }
    return result


def extract_cltk_pairs(
    connection: sqlite3.Connection,
    cltk_run_id: int,
    unknowns: dict[str, UnknownForm],
) -> tuple[dict[tuple[str, str, str], PairStats], Counter[str]]:
    pairs: dict[tuple[str, str, str], PairStats] = {}
    exclusions: Counter[str] = Counter()
    query = (
        "SELECT t.surface,t.lemma,t.upos,d.id document_id "
        "FROM cltk_document d "
        "JOIN cltk_sentence s ON s.cltk_document_id=d.id "
        "JOIN cltk_token t ON t.cltk_sentence_id=s.id "
        "WHERE d.cltk_run_id=? ORDER BY d.id,s.sequence_no,t.sequence_no"
    )
    for row in connection.execute(query, (cltk_run_id,)):
        form = token_form(str(row["surface"]))
        if form is None:
            exclusions["surface_not_one_word"] += 1
            continue
        if form not in unknowns:
            exclusions["surface_not_effective_unknown"] += 1
            continue
        raw_lemma = str(row["lemma"] or "").strip()
        lemma = normalize_lemma(raw_lemma) or ""
        if not lemma:
            exclusions["lemma_not_latin_ascii"] += 1
        upos = str(row["upos"] or "UNKNOWN").strip().upper() or "UNKNOWN"
        stats = pairs.setdefault((form, lemma, upos), PairStats())
        stats.token_count += 1
        stats.documents.add(int(row["document_id"]))
        stats.raw_lemmas[raw_lemma] += 1
    return pairs, exclusions


def superdb_hits(
    database: Path, lemmas: list[str], source_names: tuple[str, ...]
) -> dict[str, list[DictionaryHit]]:
    result: dict[str, list[DictionaryHit]] = defaultdict(list)
    connection = readonly_connection(database)
    try:
        available = {
            str(row[0]) for row in connection.execute("SELECT name FROM source")
        }
        missing = sorted(set(source_names) - available)
        if missing:
            raise CltkRoadmapError(
                f"SuperDB lacks requested source(s): {', '.join(missing)}"
            )
        for batch in chunks(lemmas):
            source_marks = ",".join("?" for _ in source_names)
            lemma_marks = ",".join("?" for _ in batch)
            query = f"""
                SELECT s.name,e.source_entry_id,e.lemma,e.lemma_norm,e.pos_std,
                       e.needs_review,e.morph_class_std,e.head_raw
                  FROM entry e JOIN source s ON s.id=e.source_id
                 WHERE s.name IN ({source_marks})
                   AND e.lemma_norm IN ({lemma_marks})
                 ORDER BY e.lemma_norm,s.name,e.source_entry_id,e.id
            """
            for row in connection.execute(query, (*source_names, *batch)):
                result[str(row["lemma_norm"])].append(
                    DictionaryHit(
                        str(row["name"]),
                        str(row["source_entry_id"] or ""),
                        str(row["lemma"]),
                        str(row["pos_std"]) if row["pos_std"] else None,
                        "source_record",
                        bool(row["needs_review"]),
                        False,
                        str(row["morph_class_std"] or row["head_raw"] or ""),
                    )
                )
    finally:
        connection.close()
    return result


def faria_hits(database: Path, lemmas: list[str]) -> dict[str, list[DictionaryHit]]:
    result: dict[str, list[DictionaryHit]] = defaultdict(list)
    connection = readonly_connection(database)
    try:
        columns = {
            str(row[1]) for row in connection.execute("PRAGMA table_info(entry)")
        }
        required = {
            "entry_id", "id", "lemma", "lemma_sort", "pos", "conf",
            "needs_review", "redirect_only", "morph_out_of_vocab", "morph_render",
        }
        if not required <= columns:
            raise CltkRoadmapError("retificado_v2 entry table has an unsupported schema")
        for batch in chunks(lemmas):
            marks = ",".join("?" for _ in batch)
            for row in connection.execute(
                "SELECT entry_id,id,lemma,lemma_sort,pos,conf,needs_review,"
                "redirect_only,morph_out_of_vocab,morph_render FROM entry "
                f"WHERE lemma_sort IN ({marks}) ORDER BY lemma_sort,entry_id",
                batch,
            ):
                details = compact_json(
                    {
                        "morph_render": str(row["morph_render"] or ""),
                        "morph_out_of_vocab": bool(row["morph_out_of_vocab"]),
                    }
                )
                result[str(row["lemma_sort"])].append(
                    DictionaryHit(
                        "faria-v2-retificado",
                        str(row["id"] or f"entry:{row['entry_id']}"),
                        str(row["lemma"]),
                        str(row["pos"]) if row["pos"] else None,
                        str(row["conf"] or "unknown"),
                        bool(row["needs_review"]),
                        bool(row["redirect_only"]),
                        details,
                    )
                )
    finally:
        connection.close()
    return result


def latin_german_hits(
    database: Path, lemmas: set[str]
) -> dict[str, list[DictionaryHit]]:
    result: dict[str, list[DictionaryHit]] = defaultdict(list)
    for entry in quantity.read_latin_german_entries(database):
        normalized = normalize_lemma(entry.lemma)
        if normalized not in lemmas:
            continue
        result[normalized].append(
            DictionaryHit(
                "latin-german",
                entry.source_entry_id,
                entry.lemma,
                entry.part_of_speech,
                "source_record",
                False,
                False,
                entry.morphology_hint or entry.head,
            )
        )
    for values in result.values():
        values.sort(key=lambda item: (item.source_entry_id, item.lemma))
    return result


POS_COMPATIBILITY = {
    "ADJ": {"ADJ"},
    "ADP": {"PREP", "ADP"},
    "ADV": {"ADV"},
    "AUX": {"VERB", "AUX"},
    "CCONJ": {"CONJ", "CCONJ"},
    "INTJ": {"INTERJ", "INTJ"},
    "NOUN": {"NOUN"},
    "NUM": {"NUM", "ADJ"},
    "PART": {"PART", "ADV"},
    "PRON": {"PRON"},
    "PROPN": {"NOUN", "PROPN"},
    "SCONJ": {"CONJ", "SCONJ"},
    "VERB": {"VERB"},
}


def canonical_pos(value: str | None) -> str | None:
    if value is None:
        return None
    upper = value.strip().upper()
    aliases = {
        "N": "NOUN", "S": "NOUN", "SUBST": "NOUN", "V": "VERB",
        "A": "ADJ", "PREPOSITION": "PREP", "CONJUNCTION": "CONJ",
    }
    return aliases.get(upper, upper) if upper else None


def assess_pos(upos: str, dictionary_pos: str | None) -> str:
    normalized = canonical_pos(dictionary_pos)
    if normalized is None or upos not in POS_COMPATIBILITY:
        return "unknown"
    return "compatible" if normalized in POS_COMPATIBILITY[upos] else "conflict"


def overall_pos(upos: str, hits: list[DictionaryHit]) -> str:
    assessments = {assess_pos(upos, hit.part_of_speech) for hit in hits}
    if "compatible" in assessments:
        return "compatible"
    if assessments == {"conflict"}:
        return "conflict"
    if "conflict" in assessments:
        return "mixed_or_unknown"
    return "unknown"


def dictionary_support(hits: list[DictionaryHit]) -> str:
    if not hits:
        return "none"
    sources = {hit.source for hit in hits}
    if len(sources) >= 2:
        return "corroborated_independent"
    if all(hit.needs_review or hit.redirect_only for hit in hits):
        return "review_only"
    return "single_source"


def merge_hits(*indexes: dict[str, list[DictionaryHit]]):
    result: dict[str, list[DictionaryHit]] = defaultdict(list)
    for index in indexes:
        for lemma, hits in index.items():
            result[lemma].extend(hits)
    for hits in result.values():
        hits.sort(key=lambda item: (item.source, item.source_entry_id, item.lemma))
    return result


def write_tsv(path: Path, fields: tuple[str, ...], rows: Iterable[dict[str, object]]) -> None:
    with atomic_text_output(path) as stream:
        writer = csv.DictWriter(
            stream, fieldnames=fields, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)


def write_jsonl(path: Path, rows: Iterable[dict[str, object]]) -> None:
    with atomic_text_output(path) as stream:
        for row in rows:
            stream.write(compact_json(row) + "\n")


def input_record(
    name: str, path: Path, *, digest: str | None = None
) -> dict[str, object]:
    return {
        "name": name,
        "path": str(path.resolve()),
        "size_bytes": path.stat().st_size,
        "sha256": digest or file_digest(path),
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("unknown_forms", type=Path)
    parser.add_argument("--stage-one-report", type=Path, required=True)
    parser.add_argument("--structural-candidates", type=Path, required=True)
    parser.add_argument("--tll-database", type=Path, required=True)
    parser.add_argument("--cltk-run-id", type=int, required=True)
    parser.add_argument("--superdb", type=Path, required=True)
    parser.add_argument(
        "--source", action="append", choices=("gaffiot", "ls_dict"), dest="sources"
    )
    parser.add_argument("--latin-german", type=Path, required=True)
    parser.add_argument("--retificado-v2", type=Path, required=True)
    parser.add_argument("--top", type=int, default=1000)
    parser.add_argument("--output-directory", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.cltk_run_id <= 0:
        raise CltkRoadmapError("--cltk-run-id must be positive")
    if arguments.top <= 0:
        raise CltkRoadmapError("--top must be positive")
    sources = tuple(dict.fromkeys(arguments.sources or ("ls_dict", "gaffiot")))

    stage_one = validate_stage_one(
        arguments.stage_one_report,
        arguments.unknown_forms,
        arguments.structural_candidates,
    )
    selection = stage_one.get("corpus", {}).get("selection_run", {})
    if not isinstance(selection, dict) or not selection.get("id"):
        raise CltkRoadmapError("stage-one report lacks its nlp_prepare selection")
    expected_tll_digest = stage_one.get("corpus", {}).get("source", {}).get("sha256")
    if not expected_tll_digest:
        raise CltkRoadmapError("stage-one report lacks its TLL source digest")
    tll_digest = file_digest(arguments.tll_database)
    if tll_digest != expected_tll_digest:
        raise CltkRoadmapError("TLL database does not match the stage-one snapshot")
    unknowns = load_unknown_forms(arguments.unknown_forms)
    structural = load_structural_candidates(arguments.structural_candidates)
    print(f"loaded {len(unknowns)} effective unknown forms", file=sys.stderr)

    connection = readonly_connection(arguments.tll_database)
    try:
        run = cltk_run_metadata(
            connection, arguments.cltk_run_id, int(selection["id"])
        )
        pairs, exclusions = extract_cltk_pairs(
            connection, int(run["cltk_run_id"]), unknowns
        )
    finally:
        connection.close()
    lemmas = sorted({key[1] for key in pairs if key[1]})
    print(
        f"found {len(pairs)} surface/lemma/POS pairs and {len(lemmas)} usable lemmas",
        file=sys.stderr,
    )

    hits = merge_hits(
        superdb_hits(arguments.superdb, lemmas, sources),
        faria_hits(arguments.retificado_v2, lemmas),
        latin_german_hits(arguments.latin_german, set(lemmas)),
    )
    form_lemma_counts: dict[str, int] = Counter(key[0] for key in pairs)
    evidence_rows: list[dict[str, object]] = []
    grouped: dict[tuple[str, str], list[tuple[str, PairStats]]] = defaultdict(list)
    for (form, lemma, upos), stats in pairs.items():
        raw_lemma = sorted(
            stats.raw_lemmas.items(), key=lambda item: (-item[1], item[0])
        )[0][0]
        lemma_hits = hits.get(lemma, []) if lemma else []
        structural_matches = structural.get(lemma, []) if lemma else []
        assessment = overall_pos(upos, lemma_hits)
        evidence_rows.append(
            {
                "form": form,
                "corpus_occurrences": unknowns[form].occurrences,
                "corpus_artifact_count": unknowns[form].artifact_count,
                "stage_one_queue": unknowns[form].queue,
                "cltk_lemma": raw_lemma,
                "cltk_lemma_norm": lemma,
                "cltk_upos": upos,
                "pilot_token_count": stats.token_count,
                "pilot_document_count": len(stats.documents),
                "surface_lemma_count": form_lemma_counts[form],
                "dictionary_status": dictionary_support(lemma_hits),
                "dictionary_source_count": len({hit.source for hit in lemma_hits}),
                "pos_assessment": assessment,
                "dictionary_hits_json": compact_json(
                    [hit.record(upos) for hit in lemma_hits]
                ),
                "structural_candidates_json": compact_json(structural_matches),
            }
        )
        if lemma:
            grouped[(lemma, upos)].append((form, stats))
    evidence_rows.sort(
        key=lambda row: (
            -int(row["corpus_occurrences"]),
            str(row["form"]),
            -int(row["pilot_token_count"]),
            str(row["cltk_lemma_norm"]),
            str(row["cltk_upos"]),
        )
    )

    candidates: list[dict[str, object]] = []
    for (lemma, upos), form_stats in grouped.items():
        unique_forms = sorted({form for form, _ in form_stats})
        lemma_hits = hits.get(lemma, [])
        source_names = sorted({hit.source for hit in lemma_hits})
        documents = set().union(*(stats.documents for _, stats in form_stats))
        corpus_upper = sum(unknowns[form].occurrences for form in unique_forms)
        pilot_count = sum(stats.token_count for _, stats in form_stats)
        ambiguous = sum(form_lemma_counts[form] > 1 for form in unique_forms)
        assessment = overall_pos(upos, lemma_hits)
        support = dictionary_support(lemma_hits)
        review_only = support == "review_only"
        structural_matches = structural.get(lemma, [])
        proper = upos == "PROPN" or any(
            bool(item["proper"]) for item in structural_matches
        ) or all(unknowns[form].queue == "proper_name" for form in unique_forms)
        abbreviation = all(
            unknowns[form].queue == "abbreviation_or_editorial"
            for form in unique_forms
        )
        all_ambiguous = all(form_lemma_counts[form] > 1 for form in unique_forms)
        if abbreviation:
            queue = "abbreviation_or_editorial"
        elif not lemma_hits:
            queue = "no_dictionary_match"
        elif assessment in {"conflict", "mixed_or_unknown"} or review_only:
            queue = "model_or_pos_review"
        elif all_ambiguous and not structural_matches:
            queue = "model_or_pos_review"
        elif proper:
            queue = "proper_name"
        else:
            queue = "common_lexeme"
        top_forms = sorted(
            (
                {
                    "form": form,
                    "corpus_occurrences": unknowns[form].occurrences,
                    "pilot_occurrences": sum(
                        stats.token_count for pair_form, stats in form_stats
                        if pair_form == form
                    ),
                    "ambiguous_cltk_lemma": form_lemma_counts[form] > 1,
                }
                for form in unique_forms
            ),
            key=lambda row: (-int(row["corpus_occurrences"]), str(row["form"])),
        )
        candidates.append(
            {
                "schema": CANDIDATE_SCHEMA,
                "key": {"lemma": lemma, "cltk_upos": upos, "proper": proper},
                "roadmap_queue": queue,
                "corpus": {
                    "corpus_occurrences_upper_bound": corpus_upper,
                    "pilot_lemma_occurrences": pilot_count,
                    "pilot_document_count": len(documents),
                    "contributing_form_count": len(unique_forms),
                    "ambiguous_form_count": ambiguous,
                    "top_forms": top_forms[:20],
                },
                "dictionary_evidence": {
                    "support": support,
                    "sources": source_names,
                    "entries": [hit.record(upos) for hit in lemma_hits],
                    "pos_assessment": assessment,
                },
                "structural_stage_candidates": structural_matches,
                "provenance": {
                    "method": "cltk_surface_to_lemma_exact_dictionary_lookup",
                    "cltk_processing_run_id": arguments.cltk_run_id,
                    "stage_one_schema": stage_one["schema"],
                },
                "automatic_promotion_allowed": False,
            }
        )
    candidates.sort(
        key=lambda row: (
            -int(row["corpus"]["corpus_occurrences_upper_bound"]),  # type: ignore[index]
            -len(row["dictionary_evidence"]["sources"]),  # type: ignore[index]
            -int(row["corpus"]["pilot_lemma_occurrences"]),  # type: ignore[index]
            str(row["key"]["lemma"]),  # type: ignore[index]
            str(row["key"]["cltk_upos"]),  # type: ignore[index]
        )
    )

    roadmap_rows: list[dict[str, object]] = []
    queue_order = (
        "common_lexeme", "proper_name", "model_or_pos_review",
        "abbreviation_or_editorial", "no_dictionary_match"
    )
    for queue in queue_order:
        selected = [row for row in candidates if row["roadmap_queue"] == queue]
        for row in selected[: arguments.top]:
            corpus = row["corpus"]  # type: ignore[assignment]
            dictionary = row["dictionary_evidence"]  # type: ignore[assignment]
            key = row["key"]  # type: ignore[assignment]
            roadmap_rows.append(
                {
                    "queue": queue,
                    "lemma": key["lemma"],
                    "cltk_upos": key["cltk_upos"],
                    "corpus_occurrences_upper_bound": corpus["corpus_occurrences_upper_bound"],
                    "pilot_lemma_occurrences": corpus["pilot_lemma_occurrences"],
                    "pilot_document_count": corpus["pilot_document_count"],
                    "contributing_form_count": corpus["contributing_form_count"],
                    "ambiguous_form_count": corpus["ambiguous_form_count"],
                    "dictionary_support": dictionary["support"],
                    "dictionary_sources": compact_json(dictionary["sources"]),
                    "dictionary_entry_count": len(dictionary["entries"]),
                    "pos_assessment": dictionary["pos_assessment"],
                    "also_in_structural_stage": bool(row["structural_stage_candidates"]),
                    "top_forms": compact_json(corpus["top_forms"]),
                }
            )

    output = arguments.output_directory
    output.mkdir(parents=True, exist_ok=True)
    paths = {
        "evidence": output / "tll-cltk-lemma-evidence.tsv.gz",
        "candidates": output / "tll-cltk-lexeme-priorities.jsonl.gz",
        "roadmap": output / "tll-cltk-roadmap.tsv",
        "report": output / "tll-cltk-coverage-report.json",
    }
    write_tsv(paths["evidence"], EVIDENCE_FIELDS, evidence_rows)
    write_jsonl(paths["candidates"], candidates)
    write_tsv(paths["roadmap"], ROADMAP_FIELDS, roadmap_rows)

    queue_counts = Counter(str(row["roadmap_queue"]) for row in candidates)
    support_counts = Counter(
        str(row["dictionary_evidence"]["support"]) for row in candidates  # type: ignore[index]
    )
    source_counts: Counter[str] = Counter()
    for row in candidates:
        for source in row["dictionary_evidence"]["sources"]:  # type: ignore[index]
            source_counts[str(source)] += 1
    input_paths = {
        "unknown_forms": arguments.unknown_forms,
        "stage_one_report": arguments.stage_one_report,
        "structural_candidates": arguments.structural_candidates,
        "tll_database": arguments.tll_database,
        "superdb": arguments.superdb,
        "latin_german": arguments.latin_german,
        "retificado_v2": arguments.retificado_v2,
    }
    report = {
        "schema": REPORT_SCHEMA,
        "policy": {
            "purpose": "reviewable possible roadmap; not an import manifest",
            "scope": "stage-one effective unknown forms observed in the selected CLTK run",
            "surface_normalization": "single TLL word token; NFC after Unicode casefold",
            "lemma_normalization": "exact ASCII lemma after ligature expansion and diacritic removal",
            "pos": "editorial signal only; conflicts are preserved",
            "corpus_occurrences": "upper bound from surface forms; not certain lemma attribution",
            "retificado_v2": "queried directly; SuperDB retificado_v2 snapshot excluded",
            "sqlite": "mode=ro&immutable=1; PRAGMA query_only=ON",
            "automatic_promotion_allowed": False,
        },
        "cltk_run": run,
        "stage_one": {
            "schema": stage_one["schema"],
            "selection_run": selection,
            "effective_unknown_forms": len(unknowns),
        },
        "counts": {
            "surface_lemma_pos_pairs": len(pairs),
            "unknown_forms_observed_in_pilot": len({key[0] for key in pairs}),
            "usable_normalized_lemmas": len(lemmas),
            "roadmap_candidate_groups": len(candidates),
            "candidate_groups_by_queue": dict(sorted(queue_counts.items())),
            "candidate_groups_by_dictionary_support": dict(sorted(support_counts.items())),
            "candidate_groups_by_dictionary_source": dict(sorted(source_counts.items())),
            "excluded_pilot_tokens": dict(sorted(exclusions.items())),
        },
        "inputs": [
            input_record(
                name, path,
                digest=tll_digest if name == "tll_database" else None,
            )
            for name, path in input_paths.items()
        ],
        "outputs": {
            name: {"path": str(path), "sha256": file_digest(path)}
            for name, path in paths.items()
            if name != "report"
        },
    }
    write_json(paths["report"], report)
    print(
        f"wrote {len(candidates)} CLTK roadmap candidates from "
        f"{len({key[0] for key in pairs})} effective unknown forms"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
