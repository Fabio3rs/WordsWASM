#!/usr/bin/env python3

"""Validate semantic alignment decisions and emit accepted lexical groups."""

from __future__ import annotations

import argparse
import gzip
import json
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator, TextIO

import jsonschema


REVIEW_SCHEMAS = frozenset(
    {
        "whitakers-words.semantic-alignment-review.v1",
        "whitakers-words.semantic-alignment-review.v2",
    }
)
DECISION_SCHEMA = "whitakers-words.semantic-alignment-decision.v1"
RESOLVED_SCHEMA = "whitakers-words.semantic-alignment-resolved.v1"
PROJECT_ROOT = Path(__file__).resolve().parents[3]
SCHEMA_PATH = PROJECT_ROOT / "schemas/semantic-alignment-decision-v1.schema.json"


class SemanticDecisionError(ValueError):
    """A semantic review queue or decision ledger is malformed."""


@dataclass(frozen=True)
class Record:
    line: int
    value: dict[str, Any]


@dataclass(frozen=True)
class Issue:
    code: str
    line: int
    message: str
    decision_id: str | None = None

    def as_json(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "code": self.code,
            "line": self.line,
            "message": self.message,
        }
        if self.decision_id is not None:
            result["decision_id"] = self.decision_id
        return result


def open_text(path: Path, mode: str) -> TextIO:
    if path.suffix == ".gz":
        return gzip.open(path, mode + "t", encoding="utf-8", newline="")
    return path.open(mode, encoding="utf-8", newline="")


def read_jsonl(path: Path) -> Iterator[Record]:
    with open_text(path, "r") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise SemanticDecisionError(f"{path}:{line_number}: invalid JSON") from error
            if not isinstance(value, dict):
                raise SemanticDecisionError(f"{path}:{line_number}: record must be an object")
            yield Record(line_number, value)


def review_key(value: dict[str, Any]) -> tuple[str, bool]:
    key = value["packet_key"]
    return str(key["ascii_lemma"]), bool(key["proper"])


def read_reviews(path: Path) -> dict[tuple[str, bool], dict[str, Any]]:
    reviews: dict[tuple[str, bool], dict[str, Any]] = {}
    for record in read_jsonl(path):
        if record.value.get("schema") not in REVIEW_SCHEMAS:
            raise SemanticDecisionError(
                f"{path}:{record.line}: expected one of {sorted(REVIEW_SCHEMAS)!r}"
            )
        key = review_key(record.value)
        if key in reviews:
            raise SemanticDecisionError(f"{path}:{record.line}: duplicate packet key {key}")
        reviews[key] = record.value
    return reviews


def load_validator() -> jsonschema.Draft202012Validator:
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def path_of(error: jsonschema.ValidationError) -> str:
    return "$" + "".join(
        f"[{item}]" if isinstance(item, int) else f".{item}"
        for item in error.absolute_path
    )


def issue(code: str, record: Record, message: str) -> Issue:
    identifier = record.value.get("decision_id")
    return Issue(code, record.line, message, identifier if isinstance(identifier, str) else None)


def part_record(review: dict[str, Any], part: str) -> dict[str, Any] | None:
    return next(
        (item for item in review["analysis"] if item["part_of_speech"] == part),
        None,
    )


def node_map(part: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {node["ref"]: node for node in part["nodes"]}


def field_consensus(nodes: list[dict[str, Any]], field: str) -> dict[str, Any]:
    votes = {
        node["source_family"]: node.get(field)
        for node in nodes
        if node.get("primary_consensus_authority") and node.get(field) is not None
    }
    counts = Counter(votes.values())
    winners = [value for value, count in counts.items() if count >= 2]
    if len(winners) == 1:
        status, value = "majority_2_of_3", winners[0]
    elif len(counts) <= 1 and counts:
        status, value = "agreement_without_quorum", next(iter(counts))
    elif counts:
        status, value = "conflict", None
    else:
        status, value = "unknown", None
    return {"status": status, "value": value, "votes": dict(sorted(votes.items()))}


def quantity_consensus(nodes: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_position: dict[int, dict[str, str]] = defaultdict(dict)
    for node in nodes:
        if not node.get("primary_consensus_authority"):
            continue
        family = node["source_family"]
        for observation in node.get("quantity_observations", []):
            by_position[int(observation["position"])][family] = observation["quantity"]
    result = []
    for position, votes in sorted(by_position.items()):
        counts = Counter(votes.values())
        winners = [value for value, count in counts.items() if count >= 2]
        if len(winners) == 1:
            status, value = "majority_2_of_3", winners[0]
        elif len(counts) <= 1:
            status, value = "agreement_without_quorum", next(iter(counts))
        else:
            status, value = "conflict", None
        result.append(
            {
                "position": position,
                "status": status,
                "value": value,
                "votes": dict(sorted(votes.items())),
            }
        )
    return result


def resolved_record(
    decision: dict[str, Any], review: dict[str, Any], nodes: list[dict[str, Any]],
) -> dict[str, Any]:
    words_targets = sorted(
        int(node["ref"].split(":", 1)[1])
        for node in nodes
        if node["ref"].startswith("words:")
    )
    return {
        "schema": RESOLVED_SCHEMA,
        "alignment_id": decision["decision_id"],
        "packet_key": review["packet_key"],
        "analysis_revision": review["revision"],
        "part_of_speech": decision["target"]["part_of_speech"],
        "sense_summary": decision["sense_summary"],
        "members": decision["target"]["members"],
        "words_targets": words_targets,
        "lexical_action_hint": (
            "merge_existing" if len(words_targets) == 1
            else "review_multiple_words_targets" if words_targets
            else "new_lexeme_candidate"
        ),
        "consensus": {
            "gender": field_consensus(nodes, "gender"),
            "indeclinable": field_consensus(nodes, "indeclinable"),
            "vowel_quantity": quantity_consensus(nodes),
        },
        "review": decision["review"],
        "automatic_promotion_allowed": False,
    }


def validate(
    reviews: dict[tuple[str, bool], dict[str, Any]], decisions: tuple[Record, ...]
) -> tuple[dict[str, Any], tuple[dict[str, Any], ...]]:
    validator = load_validator()
    problems: list[Issue] = []
    valid_contexts: dict[int, tuple[dict[str, Any], dict[str, Any], list[dict[str, Any]]]] = {}
    seen_decision_ids: dict[str, int] = {}
    accepted_members: dict[tuple[tuple[str, bool], str, str], list[int]] = defaultdict(list)

    for index, record in enumerate(decisions):
        errors = sorted(
            validator.iter_errors(record.value),
            key=lambda error: (list(error.absolute_path), error.message),
        )
        if errors:
            problems.append(issue("schema_error", record, f"{path_of(errors[0])}: {errors[0].message}"))
            continue
        decision_id = record.value["decision_id"]
        if decision_id in seen_decision_ids:
            problems.append(issue("duplicate_decision_id", record, f"decision_id also appears on line {seen_decision_ids[decision_id]}"))
            continue
        seen_decision_ids[decision_id] = record.line
        target = record.value["target"]
        key = (target["packet_key"]["ascii_lemma"], target["packet_key"]["proper"])
        review = reviews.get(key)
        if review is None:
            problems.append(issue("unknown_packet", record, f"no review packet for {key}"))
            continue
        if target["analysis_revision"] != review["revision"]:
            problems.append(issue("stale_revision", record, "analysis_revision differs from the current review record"))
            continue
        part = part_record(review, target["part_of_speech"])
        if part is None:
            problems.append(issue("unknown_part_queue", record, f"review has no {target['part_of_speech']} queue"))
            continue
        nodes_by_ref = node_map(part)
        unknown = sorted(set(target["members"]) - set(nodes_by_ref))
        if unknown:
            problems.append(issue("unknown_member", record, f"members not present in queue: {', '.join(unknown)}"))
            continue
        nodes = [nodes_by_ref[member] for member in target["members"]]
        if record.value["disposition"] == "accept_alignment":
            families = [node["source_family"] for node in nodes]
            if len(families) != len(set(families)):
                problems.append(issue("source_family_collision", record, "accepted alignment contains two entries from one source family"))
                continue
            for member in target["members"]:
                accepted_members[(key, target["part_of_speech"], member)].append(index)
        valid_contexts[index] = (review, part, nodes)

    invalid_indexes: set[int] = set()
    for (key, part, member), indexes in accepted_members.items():
        if len(indexes) < 2:
            continue
        invalid_indexes.update(indexes)
        for index in indexes:
            problems.append(
                issue(
                    "overlapping_accepted_alignment",
                    decisions[index],
                    f"member {member!r} is accepted more than once for {key} {part}",
                )
            )

    resolved = []
    dispositions = Counter()
    for index, record in enumerate(decisions):
        if index not in valid_contexts or index in invalid_indexes:
            continue
        dispositions[record.value["disposition"]] += 1
        if record.value["disposition"] != "accept_alignment":
            continue
        review, _, nodes = valid_contexts[index]
        resolved.append(resolved_record(record.value, review, nodes))
    resolved.sort(key=lambda item: (item["packet_key"]["ascii_lemma"], item["part_of_speech"], item["alignment_id"]))
    report = {
        "schema": "whitakers-words.semantic-alignment-validation.v1",
        "valid": not problems,
        "review_packets": len(reviews),
        "decision_records": len(decisions),
        "valid_decisions": len(valid_contexts) - len(invalid_indexes),
        "resolved_alignments": len(resolved),
        "dispositions": dict(sorted(dispositions.items())),
        "errors": [problem.as_json() for problem in problems],
        "automatic_promotion_allowed": False,
    }
    return report, tuple(resolved)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("review_queue", type=Path)
    parser.add_argument("decision_ledger", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="accepted alignment JSONL or JSONL.GZ")
    parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    reviews = read_reviews(arguments.review_queue)
    decisions = tuple(read_jsonl(arguments.decision_ledger))
    report, resolved = validate(reviews, decisions)
    with open_text(arguments.output, "w") as output:
        for record in resolved:
            output.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"validated {report['valid_decisions']} decisions; wrote {len(resolved)} accepted alignments")
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
