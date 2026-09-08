#!/usr/bin/env python3

"""Create a stratified gold queue and calibrate lexical embedding thresholds."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import Any, Iterable, Iterator, TextIO

import jsonschema

import lexical_embedding_db as lexical


GOLD_SCHEMA = "whitakers-words.lexical-embedding-gold.v1"
PROJECT_ROOT = Path(__file__).resolve().parents[3]
SCHEMA_PATH = PROJECT_ROOT / "schemas/lexical-embedding-gold-v1.schema.json"
TARGETS = {
    "words_target": 200,
    "without_words": 150,
    "homograph_or_quantity_conflict": 100,
    "rejected_edge": 75,
    "cross_language_without_bridge": 75,
}


def open_text(path: Path, mode: str) -> TextIO:
    if path.suffix == ".gz":
        return gzip.open(path, mode + "t", encoding="utf-8", newline="")
    return path.open(mode, encoding="utf-8", newline="")


def read_jsonl(path: Path) -> Iterator[dict[str, Any]]:
    with open_text(path, "r") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise lexical.LexicalEmbeddingError(f"{path}:{line_number}: invalid JSON") from error
            if not isinstance(value, dict):
                raise lexical.LexicalEmbeddingError(f"{path}:{line_number}: expected object")
            yield value


def existing_pairs(path: Path | None) -> dict[tuple[str, str], tuple[str, str | None]]:
    if path is None:
        return {}
    result = {}
    for review in read_jsonl(path):
        if review.get("schema") not in {
            "whitakers-words.semantic-alignment-review.v1",
            "whitakers-words.semantic-alignment-review.v2",
        }:
            raise lexical.LexicalEmbeddingError(f"{path}: unexpected review schema")
        for analysis in review.get("analysis", []):
            for pair in analysis.get("pair_candidates", []):
                key = lexical.pair_key(str(pair["left"]), str(pair["right"]))
                edge = pair.get("alignment_edge", {}).get("status")
                result[key] = (str(pair.get("relation", "")), str(edge) if edge else None)
    return result


def candidate_revision(value: dict[str, Any]) -> str:
    material = {
        key: value[key]
        for key in sorted(value)
        if key not in {"candidate_revision", "label", "reviewer", "note"}
    }
    return lexical.sha256_text(lexical.canonical_json(material))


def spread(values: list[dict[str, Any]], count: int) -> list[dict[str, Any]]:
    if len(values) < count:
        raise lexical.LexicalEmbeddingError(
            f"stratum has only {len(values)} candidates; {count} required"
        )
    values.sort(key=lambda item: (-item["embedding"]["max_cosine"], item["left_entry_ref"], item["right_entry_ref"]))
    if count == 1:
        return [values[len(values) // 2]]
    indexes = [round(index * (len(values) - 1) / (count - 1)) for index in range(count)]
    return [values[index] for index in indexes]


def sample(arguments: argparse.Namespace) -> dict[str, Any]:
    connection = lexical.connect_evidence(arguments.evidence, readonly=True)
    from rank_lexical_embeddings import load_run

    run = load_run(connection, arguments.run_id)
    relations = existing_pairs(arguments.review_queue)
    entries = {
        str(row["entry_ref"]): row
        for row in connection.execute(
            "SELECT entry_ref,source_family,ascii_lemma,proper,homograph_number FROM entry"
        )
    }
    pools: dict[str, list[dict[str, Any]]] = {key: [] for key in TARGETS}
    rows = connection.execute(
        "SELECT * FROM candidate_score WHERE run_id=? ORDER BY max_cosine DESC,left_entry_ref,right_entry_ref",
        (run["run_id"],),
    )
    candidates = []
    for row in rows:
        left = entries[str(row["left_entry_ref"])]
        right = entries[str(row["right_entry_ref"])]
        relation, edge = relations.get(
            lexical.pair_key(str(row["left_entry_ref"]), str(row["right_entry_ref"])),
            (None, None),
        )
        margins = [
            float(value) for value in (row["left_margin"], row["right_margin"])
            if value is not None
        ]
        value = {
            "schema": GOLD_SCHEMA,
            "candidate_revision": "",
            "run_id": str(run["run_id"]),
            "packet_key": {
                "ascii_lemma": str(row["ascii_lemma"]),
                "proper": bool(row["proper"]),
            },
            "left_entry_ref": str(row["left_entry_ref"]),
            "right_entry_ref": str(row["right_entry_ref"]),
            "stratum": "",
            "embedding": {
                "max_cosine": float(row["max_cosine"]),
                "top3_mean_cosine": float(row["top3_mean_cosine"]),
                "left_rank": int(row["left_rank"]) if row["left_rank"] is not None else None,
                "right_rank": int(row["right_rank"]) if row["right_rank"] is not None else None,
                "mutual_top1": bool(row["mutual_top1"]),
                "minimum_margin": min(margins) if len(margins) == 2 else None,
                "blocking_reason": str(row["blocking_reason"]) if row["blocking_reason"] else None,
            },
            "existing_relation": relation,
            "existing_edge_status": edge,
            "label": "pending",
            "reviewer": "",
            "note": "",
        }
        signals = json.loads(row["signals_json"])
        candidates.append((value, left, right, signals))
    connection.close()

    # Exclusive priority keeps the requested 600 records disjoint.
    remaining = list(candidates)
    selectors = [
        ("homograph_or_quantity_conflict", lambda item: item[1]["homograph_number"] is not None or item[2]["homograph_number"] is not None or bool(item[3]["quantity"]["conflict_positions"])),
        ("rejected_edge", lambda item: str(item[0].get("existing_edge_status") or "").startswith("rejected")),
        ("cross_language_without_bridge", lambda item: item[0].get("existing_relation") == "insufficient_cross_language_evidence"),
        ("words_target", lambda item: "current-words" in {item[1]["source_family"], item[2]["source_family"]}),
        ("without_words", lambda item: "current-words" not in {item[1]["source_family"], item[2]["source_family"]}),
    ]
    selected = []
    for stratum, selector in selectors:
        pool = [item[0] for item in remaining if selector(item)]
        chosen = spread(pool, TARGETS[stratum])
        chosen_keys = {(item["left_entry_ref"], item["right_entry_ref"]) for item in chosen}
        for item in chosen:
            item["stratum"] = stratum
            item["candidate_revision"] = candidate_revision(item)
        selected.extend(chosen)
        remaining = [
            item for item in remaining
            if (item[0]["left_entry_ref"], item[0]["right_entry_ref"]) not in chosen_keys
        ]
    selected.sort(key=lambda item: (item["stratum"], item["packet_key"]["ascii_lemma"], item["left_entry_ref"], item["right_entry_ref"]))
    with open_text(arguments.output, "w") as output:
        for item in selected:
            output.write(json.dumps(item, ensure_ascii=False, separators=(",", ":")) + "\n")
    return {
        "schema": "whitakers-words.lexical-embedding-gold-sample-report.v1",
        "run_id": str(run["run_id"]),
        "records": len(selected),
        "strata": dict(sorted(Counter(item["stratum"] for item in selected).items())),
        "output": str(arguments.output),
    }


def validate_gold(path: Path) -> list[dict[str, Any]]:
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    validator = jsonschema.Draft202012Validator(schema)
    result = []
    for line_number, value in enumerate(read_jsonl(path), 1):
        errors = sorted(validator.iter_errors(value), key=lambda error: list(error.absolute_path))
        if errors:
            raise lexical.LexicalEmbeddingError(
                f"{path}:{line_number}: schema error: {errors[0].message}"
            )
        if value["candidate_revision"] != candidate_revision(value):
            raise lexical.LexicalEmbeddingError(f"{path}:{line_number}: stale candidate revision")
        if value["label"] == "pending" or not value["reviewer"].strip() or not value["note"].strip():
            raise lexical.LexicalEmbeddingError(f"{path}:{line_number}: review is incomplete")
        result.append(value)
    return result


def split(records: Iterable[dict[str, Any]], seed: str = "lexical-embedding-gold-v1") -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    development, holdout = [], []
    for record in records:
        key = record["packet_key"]
        group = f"{key['ascii_lemma']}:{int(key['proper'])}"
        bucket = int(hashlib.sha256(f"{seed}:{group}".encode()).hexdigest()[:8], 16) % 100
        (development if bucket < 70 else holdout).append(record)
    return development, holdout


def selected(record: dict[str, Any], threshold: float, margin: float) -> bool:
    embedding = record["embedding"]
    return bool(
        embedding["blocking_reason"] is None
        and embedding["mutual_top1"]
        and embedding["minimum_margin"] is not None
        and embedding["max_cosine"] >= threshold
        and embedding["minimum_margin"] >= margin
    )


def metrics(records: list[dict[str, Any]], threshold: float, margin: float) -> dict[str, Any]:
    labeled = [record for record in records if record["label"] != "uncertain"]
    positives = sum(record["label"] == "same_lexeme" for record in labeled)
    proposed = [record for record in labeled if selected(record, threshold, margin)]
    true_positive = sum(record["label"] == "same_lexeme" for record in proposed)
    false_positive = len(proposed) - true_positive
    positive_records = [record for record in labeled if record["label"] == "same_lexeme"]
    reciprocal_ranks = []
    reciprocal_top1 = 0
    for record in positive_records:
        embedding = record["embedding"]
        ranks = (embedding.get("left_rank"), embedding.get("right_rank"))
        if all(isinstance(rank, int) and rank > 0 for rank in ranks):
            reciprocal_ranks.append(1.0 / max(ranks))
            reciprocal_top1 += int(ranks == (1, 1))
        else:
            reciprocal_ranks.append(0.0)
    return {
        "labeled": len(labeled),
        "positives": positives,
        "proposed": len(proposed),
        "true_positive": true_positive,
        "false_positive": false_positive,
        "precision": true_positive / len(proposed) if proposed else 0.0,
        "recall": true_positive / positives if positives else 0.0,
        "mean_reciprocal_rank": sum(reciprocal_ranks) / positives if positives else 0.0,
        "reciprocal_top1_recall": reciprocal_top1 / positives if positives else 0.0,
    }


def baseline_metrics(records: list[dict[str, Any]]) -> dict[str, Any]:
    labeled = [record for record in records if record["label"] != "uncertain"]
    positives = sum(record["label"] == "same_lexeme" for record in labeled)
    proposed = [
        record for record in labeled
        if record.get("existing_edge_status") == "selected_mutual_unique"
    ]
    true_positive = sum(record["label"] == "same_lexeme" for record in proposed)
    return {
        "labeled": len(labeled),
        "positives": positives,
        "proposed": len(proposed),
        "true_positive": true_positive,
        "false_positive": len(proposed) - true_positive,
        "precision": true_positive / len(proposed) if proposed else 0.0,
        "recall": true_positive / positives if positives else 0.0,
    }


def evaluate(arguments: argparse.Namespace) -> tuple[dict[str, Any], dict[str, Any]]:
    records = validate_gold(arguments.gold)
    expected_records = getattr(arguments, "expected_records", 600)
    if expected_records and len(records) != expected_records:
        raise lexical.LexicalEmbeddingError(
            f"gold ledger has {len(records)} records; expected {expected_records}"
        )
    if expected_records == 600:
        actual_strata = Counter(record["stratum"] for record in records)
        if actual_strata != Counter(TARGETS):
            raise lexical.LexicalEmbeddingError(
                f"gold strata differ from required allocation: {dict(actual_strata)}"
            )
    run_ids = {record["run_id"] for record in records}
    if len(run_ids) != 1:
        raise lexical.LexicalEmbeddingError("gold records must belong to exactly one embedding run")
    development, holdout = split(records)
    viable = []
    for threshold_step in range(0, 101):
        threshold = threshold_step / 100
        for margin_step in range(0, 21):
            margin = margin_step / 100
            result = metrics(development, threshold, margin)
            if result["proposed"] and result["precision"] >= arguments.minimum_precision:
                viable.append((result["recall"], result["precision"], -threshold, -margin, threshold, margin, result))
    if viable:
        _, _, _, _, threshold, margin, development_metrics = max(viable)
        holdout_metrics = metrics(holdout, threshold, margin)
        homograph_metrics = metrics(
            [record for record in holdout if record["stratum"] == "homograph_or_quantity_conflict"],
            threshold, margin,
        )
        promotion = bool(
            holdout_metrics["proposed"]
            and holdout_metrics["precision"] >= arguments.minimum_precision
            and homograph_metrics["false_positive"] == 0
        )
    else:
        threshold, margin = 1.0, 1.0
        development_metrics = metrics(development, threshold, margin)
        holdout_metrics = metrics(holdout, threshold, margin)
        homograph_metrics = metrics(
            [record for record in holdout if record["stratum"] == "homograph_or_quantity_conflict"],
            threshold, margin,
        )
        promotion = False
    run_id = next(iter(run_ids))
    configuration = {
        "schema": "whitakers-words.lexical-embedding-calibration.v1",
        "run_id": run_id,
        "minimum_precision": arguments.minimum_precision,
        "cosine_threshold": threshold,
        "minimum_margin": margin,
        "promotion_allowed": promotion,
        "gold_sha256": lexical.sha256_file(arguments.gold),
        "policy": "mutual_top1_and_no_structural_blocker",
    }
    report = {
        "schema": "whitakers-words.lexical-embedding-calibration-report.v1",
        "configuration": configuration,
        "records": len(records),
        "development": development_metrics,
        "holdout": holdout_metrics,
        "holdout_homographs": homograph_metrics,
        "baseline": {
            "development": baseline_metrics(development),
            "holdout": baseline_metrics(holdout),
        },
    }
    return report, configuration


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    sample_parser = commands.add_parser("sample")
    sample_parser.add_argument("evidence", type=Path)
    sample_parser.add_argument("--run-id")
    sample_parser.add_argument("--review-queue", type=Path)
    sample_parser.add_argument("--output", type=Path, required=True)
    sample_parser.add_argument("--report", type=Path, required=True)
    evaluate_parser = commands.add_parser("evaluate")
    evaluate_parser.add_argument("gold", type=Path)
    evaluate_parser.add_argument("--minimum-precision", type=float, default=0.995)
    evaluate_parser.add_argument("--expected-records", type=int, default=600)
    evaluate_parser.add_argument("--output", type=Path, required=True, help="calibration configuration JSON")
    evaluate_parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.command == "sample":
        report = sample(arguments)
    else:
        if not 0.0 < arguments.minimum_precision <= 1.0:
            raise lexical.LexicalEmbeddingError("--minimum-precision must be in (0,1]")
        report, configuration = evaluate(arguments)
        arguments.output.write_text(
            json.dumps(configuration, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
