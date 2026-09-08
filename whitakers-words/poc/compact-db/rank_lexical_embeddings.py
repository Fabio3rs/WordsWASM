#!/usr/bin/env python3

"""Rank cross-dictionary entries by exact cosine inside lexical blocks."""

from __future__ import annotations

import argparse
import json
import sqlite3
from collections import Counter, defaultdict
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any, Iterable

import numpy as np

import lexical_embedding_db as lexical


@dataclass(frozen=True)
class Entry:
    ref: str
    family: str
    lemma: str
    proper: bool
    part: str | None
    gender: str | None
    indeclinable: bool | None
    homograph: int | None
    current_targets: tuple[int, ...]
    quantities: tuple[tuple[int, str], ...]
    basic_forms: tuple[str, ...]


@dataclass
class Candidate:
    left: Entry
    right: Entry
    ascii_lemma: str
    proper: bool
    packet_revision: str | None
    blocker: str | None
    maximum: float
    top3_mean: float
    best_left_document: str
    best_right_document: str
    signals: dict[str, Any]
    left_rank: int | None = None
    right_rank: int | None = None
    left_margin: float | None = None
    right_margin: float | None = None
    mutual_top1: bool = False


def load_run(connection: sqlite3.Connection, requested: str | None) -> sqlite3.Row:
    if requested:
        row = connection.execute(
            "SELECT * FROM embedding_run WHERE run_id=?", (requested,)
        ).fetchone()
    else:
        row = connection.execute(
            "SELECT * FROM embedding_run WHERE status='complete' "
            "ORDER BY completed_at DESC,run_id DESC LIMIT 1"
        ).fetchone()
    if row is None:
        raise lexical.LexicalEmbeddingError("no matching embedding run")
    if row["status"] != "complete":
        raise lexical.LexicalEmbeddingError(f"embedding run {row['run_id']} is not complete")
    return row


def parse_entry(row: sqlite3.Row, basic_forms: tuple[str, ...] = ()) -> Entry:
    return Entry(
        str(row["entry_ref"]), str(row["source_family"]), str(row["ascii_lemma"]),
        bool(row["proper"]), str(row["part_of_speech"]) if row["part_of_speech"] else None,
        str(row["gender"]) if row["gender"] else None,
        bool(row["indeclinable"]) if row["indeclinable"] is not None else None,
        int(row["homograph_number"]) if row["homograph_number"] is not None else None,
        tuple(int(value) for value in json.loads(row["current_words_match_ids_json"])),
        tuple(
            sorted(
                (int(item["position"]), str(item["quantity"]))
                for item in json.loads(row["quantity_json"])
            )
        ), basic_forms,
    )


def candidate_pairs(connection: sqlite3.Connection) -> list[tuple[str, str, str, bool, str | None]]:
    grouped: dict[tuple[str, bool], list[tuple[str, str]]] = defaultdict(list)
    entries_by_lemma: dict[tuple[str, bool], list[str]] = defaultdict(list)
    for row in connection.execute(
        "SELECT entry_ref,ascii_lemma,proper,packet_revision FROM entry_bucket "
        "ORDER BY ascii_lemma,proper,entry_ref"
    ):
        key = (str(row["ascii_lemma"]), bool(row["proper"]))
        grouped[key].append((str(row["entry_ref"]), str(row["packet_revision"])))
        entries_by_lemma[key].append(str(row["entry_ref"]))
    result: dict[tuple[str, str], tuple[str, str, str, bool, str | None]] = {}
    for (lemma, proper), members in grouped.items():
        revisions = {revision for _, revision in members}
        if len(revisions) != 1:
            raise lexical.LexicalEmbeddingError(
                f"comparison bucket {(lemma, proper)} has inconsistent packet revisions"
            )
        revision = next(iter(revisions))
        for index, (left, _) in enumerate(members):
            for right, _ in members[index + 1:]:
                first, second = lexical.pair_key(left, right)
                result[(first, second)] = (first, second, lemma, proper, revision)
    for row in connection.execute(
        "SELECT a.entry_ref,a.target_ascii,e.proper FROM entry_alias a "
        "JOIN entry e ON e.entry_ref=a.entry_ref "
        "WHERE a.pure_alias=1 AND a.target_ascii IS NOT NULL"
    ):
        for target in entries_by_lemma.get((str(row["target_ascii"]), bool(row["proper"])), []):
            if target != row["entry_ref"]:
                first, second = lexical.pair_key(str(row["entry_ref"]), target)
                result.setdefault(
                    (first, second),
                    (first, second, str(row["target_ascii"]), bool(row["proper"]), None),
                )
    return sorted(result.values())


def document_rows(connection: sqlite3.Connection) -> dict[str, list[sqlite3.Row]]:
    result: dict[str, list[sqlite3.Row]] = defaultdict(list)
    for row in connection.execute(
        "SELECT document_ref,entry_ref,language,quality,semantic_text "
        "FROM semantic_document ORDER BY entry_ref,document_ref"
    ):
        result[str(row["entry_ref"])].append(row)
    return result


def load_vectors(
    cache: sqlite3.Connection,
    rows: Iterable[sqlite3.Row],
    model_digest: str,
    prompt_version: str,
    instruction: str,
    dimension: int,
) -> tuple[list[str], np.ndarray]:
    identifiers = []
    vectors = []
    for row in rows:
        formatted = lexical.format_embedding_input(
            str(row["semantic_text"]), str(row["language"]), instruction
        )
        input_hash = lexical.sha256_text(formatted)
        stored = cache.execute(
            "SELECT dimension,dtype,normalized,vector FROM embedding "
            "WHERE document_ref=? AND input_sha256=? AND model_digest=? AND prompt_version=?",
            (row["document_ref"], input_hash, model_digest, prompt_version),
        ).fetchone()
        if stored is None:
            raise lexical.LexicalEmbeddingError(
                f"missing cached embedding for {row['document_ref']}"
            )
        if int(stored["dimension"]) != dimension or stored["dtype"] != "float32-le" or int(stored["normalized"]) != 1:
            raise lexical.LexicalEmbeddingError(
                f"incompatible cached embedding for {row['document_ref']}"
            )
        vector = np.frombuffer(stored["vector"], dtype="<f4")
        if len(vector) != dimension:
            raise lexical.LexicalEmbeddingError(
                f"corrupt cached embedding for {row['document_ref']}"
            )
        identifiers.append(str(row["document_ref"]))
        vectors.append(vector)
    if not vectors:
        return [], np.empty((0, dimension), dtype=np.float32)
    return identifiers, np.stack(vectors)


def quantity_signal(left: Entry, right: Entry) -> dict[str, list[int]]:
    first, second = dict(left.quantities), dict(right.quantities)
    positions = sorted(set(first) & set(second))
    return {
        "comparable_positions": positions,
        "agreement_positions": [p for p in positions if first[p] == second[p]],
        "conflict_positions": [p for p in positions if first[p] != second[p]],
    }


def structural_signals(left: Entry, right: Entry, qualities: tuple[set[str], set[str]]) -> tuple[str | None, dict[str, Any]]:
    quantity = quantity_signal(left, right)
    pos_conflict = bool(left.part and right.part and left.part != right.part)
    proper_conflict = left.proper != right.proper
    gender_conflict = bool(left.gender and right.gender and left.gender != right.gender)
    indeclinable_conflict = bool(
        left.indeclinable is not None
        and right.indeclinable is not None
        and left.indeclinable != right.indeclinable
    )
    crossref_only = all(values == {"crossref_only"} for values in qualities)
    blocker = None
    if proper_conflict:
        blocker = "proper_conflict"
    elif pos_conflict:
        blocker = "part_of_speech_conflict"
    elif gender_conflict:
        blocker = "gender_conflict"
    elif indeclinable_conflict:
        blocker = "indeclinable_conflict"
    elif quantity["conflict_positions"]:
        blocker = "quantity_conflict"
    elif crossref_only:
        blocker = "crossref_only_documents"
    return blocker, {
        "same_ascii_lemma": left.lemma == right.lemma,
        "left_part_of_speech": left.part,
        "right_part_of_speech": right.part,
        "part_of_speech_conflict": pos_conflict,
        "left_gender": left.gender,
        "right_gender": right.gender,
        "gender_conflict": gender_conflict,
        "left_indeclinable": left.indeclinable,
        "right_indeclinable": right.indeclinable,
        "indeclinable_conflict": indeclinable_conflict,
        "shared_basic_forms": sorted(set(left.basic_forms) & set(right.basic_forms)),
        "shared_current_words_targets": sorted(set(left.current_targets) & set(right.current_targets)),
        "quantity": quantity,
        "left_homograph_number": left.homograph,
        "right_homograph_number": right.homograph,
    }


def score_pair(
    left: Entry,
    right: Entry,
    left_rows: list[sqlite3.Row],
    right_rows: list[sqlite3.Row],
    left_embeddings: tuple[list[str], np.ndarray],
    right_embeddings: tuple[list[str], np.ndarray],
    ascii_lemma: str,
    proper: bool,
    packet_revision: str | None,
) -> Candidate | None:
    if left.family == right.family or not left_rows or not right_rows:
        return None
    left_ids, left_vectors = left_embeddings
    right_ids, right_vectors = right_embeddings
    matrix = np.matmul(left_vectors, right_vectors.T)
    flat = matrix.ravel()
    best_flat = int(np.argmax(flat))
    left_index, right_index = np.unravel_index(best_flat, matrix.shape)
    best = max(-1.0, min(1.0, float(matrix[left_index, right_index])))
    top_count = min(3, len(flat))
    top_values = np.partition(flat, len(flat) - top_count)[-top_count:]
    top3 = max(-1.0, min(1.0, float(np.mean(top_values))))
    qualities = (
        {str(row["quality"]) for row in left_rows},
        {str(row["quality"]) for row in right_rows},
    )
    blocker, signals = structural_signals(left, right, qualities)
    return Candidate(
        left, right, ascii_lemma, proper, packet_revision, blocker, best, top3,
        left_ids[left_index], right_ids[right_index], signals,
    )


def assign_ranks(candidates: list[Candidate]) -> None:
    directions: dict[tuple[str, str], list[tuple[float, str, Candidate, str]]] = defaultdict(list)
    for candidate in candidates:
        if candidate.blocker is not None:
            continue
        directions[(candidate.left.ref, candidate.right.family)].append(
            (candidate.maximum, candidate.right.ref, candidate, "left")
        )
        directions[(candidate.right.ref, candidate.left.family)].append(
            (candidate.maximum, candidate.left.ref, candidate, "right")
        )
    for values in directions.values():
        values.sort(key=lambda item: (-item[0], item[1]))
        for index, (score, _, candidate, side) in enumerate(values):
            rank = 1 + sum(1 for previous in values[:index] if previous[0] > score)
            next_score = values[1][0] if index == 0 and len(values) > 1 else 0.0
            margin = score - next_score if rank == 1 else score - values[0][0]
            if side == "left":
                candidate.left_rank = rank
                candidate.left_margin = margin
            else:
                candidate.right_rank = rank
                candidate.right_margin = margin
    for candidate in candidates:
        candidate.mutual_top1 = bool(
            candidate.blocker is None
            and candidate.left_rank == 1
            and candidate.right_rank == 1
            and candidate.left_margin is not None
            and candidate.right_margin is not None
            and candidate.left_margin > 0.0
            and candidate.right_margin > 0.0
        )


def rank(arguments: argparse.Namespace) -> dict[str, Any]:
    evidence = lexical.connect_evidence(arguments.evidence)
    run = load_run(evidence, arguments.run_id)
    cache = lexical.connect_evidence(arguments.cache, readonly=True)
    forms: dict[str, list[str]] = defaultdict(list)
    for row in evidence.execute(
        "SELECT entry_ref,form_ascii FROM entry_form "
        "WHERE form_ascii IS NOT NULL AND form_ascii <> '' ORDER BY entry_ref,ordinal"
    ):
        forms[str(row["entry_ref"])].append(str(row["form_ascii"]))
    entries = {
        str(row["entry_ref"]): parse_entry(
            row, tuple(dict.fromkeys(forms.get(str(row["entry_ref"]), [])))
        )
        for row in evidence.execute("SELECT * FROM entry ORDER BY entry_ref")
    }
    documents = document_rows(evidence)
    @lru_cache(maxsize=512)
    def embeddings_for(entry_ref: str) -> tuple[list[str], np.ndarray]:
        return load_vectors(
            cache, documents.get(entry_ref, []), str(run["model_digest"]),
            str(run["prompt_version"]), str(run["instruction"]),
            int(run["dimension"]),
        )

    candidates = []
    counts: Counter[str] = Counter()
    for left_ref, right_ref, ascii_lemma, proper, packet_revision in candidate_pairs(evidence):
        if entries[left_ref].family == entries[right_ref].family:
            counts["skipped_same_family_or_missing_text"] += 1
            continue
        candidate = score_pair(
            entries[left_ref], entries[right_ref], documents.get(left_ref, []),
            documents.get(right_ref, []), embeddings_for(left_ref),
            embeddings_for(right_ref), ascii_lemma, proper, packet_revision,
        )
        if candidate is None:
            counts["skipped_same_family_or_missing_text"] += 1
            continue
        candidates.append(candidate)
        counts["candidate_pairs"] += 1
        counts[f"blocking:{candidate.blocker or 'none'}"] += 1
    assign_ranks(candidates)
    counts["mutual_top1"] = sum(candidate.mutual_top1 for candidate in candidates)
    run_id = str(run["run_id"])
    with evidence:
        evidence.execute("DELETE FROM candidate_score WHERE run_id=?", (run_id,))
        evidence.executemany(
            "INSERT INTO candidate_score VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            [
                (
                    run_id, candidate.left.ref, candidate.right.ref,
                    candidate.ascii_lemma, int(candidate.proper), candidate.packet_revision,
                    candidate.blocker,
                    round(candidate.maximum, 7), round(candidate.top3_mean, 7),
                    candidate.best_left_document, candidate.best_right_document,
                    candidate.left_rank, candidate.right_rank,
                    round(candidate.left_margin, 7) if candidate.left_margin is not None else None,
                    round(candidate.right_margin, 7) if candidate.right_margin is not None else None,
                    int(candidate.mutual_top1), lexical.canonical_json(candidate.signals),
                )
                for candidate in candidates
            ],
        )
    evidence.close()
    cache.close()
    return {
        "schema": "whitakers-words.lexical-embedding-ranking-report.v1",
        "run_id": run_id,
        "evidence": str(arguments.evidence),
        "cache": str(arguments.cache),
        "counts": dict(sorted(counts.items())),
        "policy": {
            "comparison": "exact_cosine_within_ascii_lemma_or_explicit_pure_alias",
            "same_source_family": "excluded",
            "automatic_promotion_allowed": False,
        },
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--run-id")
    parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    report = rank(arguments)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"ranked {report['counts'].get('candidate_pairs', 0)} pairs; "
        f"{report['counts'].get('mutual_top1', 0)} are reciprocal top-1 hints"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
