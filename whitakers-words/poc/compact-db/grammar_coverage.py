#!/usr/bin/env python3
"""Compile deterministic RuleId coverage from native search documents."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


def compile_report(
    documents: Iterable[dict[str, Any]], total_rules: int, corpus: str
) -> str:
    if total_rules <= 0:
        raise ValueError("rule count must be positive")

    witnesses: dict[int, set[str]] = defaultdict(set)
    query_count = 0
    for document in documents:
        query_count += 1
        try:
            query = document["query"]["text"]
            hits = document["hits"]
        except (KeyError, TypeError) as error:
            raise ValueError("invalid search document") from error
        if not isinstance(query, str) or not isinstance(hits, list):
            raise ValueError("invalid search document")
        for hit in hits:
            rule_id = hit.get("ruleId")
            if rule_id is None:
                continue
            if (
                not isinstance(rule_id, int)
                or isinstance(rule_id, bool)
                or not 0 <= rule_id < total_rules
            ):
                raise ValueError(f"RuleId outside dataset: {rule_id}")
            witnesses[rule_id].add(query)

    observed = sorted(witnesses)
    missing = sorted(set(range(total_rules)) - set(observed))
    report = {
        "schema": "words.grammar-coverage",
        "schemaVersion": 1,
        "corpus": corpus,
        "counts": {
            "rules": total_rules,
            "observed": len(observed),
            "missing": len(missing),
            "queries": query_count,
        },
        "observedRuleIds": observed,
        "missingRuleIds": missing,
        "witnesses": {
            str(rule_id): sorted(witnesses[rule_id]) for rule_id in observed
        },
    }
    return json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--rules", type=int, required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    documents = [
        json.loads(line)
        for line in arguments.input.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    report = compile_report(documents, arguments.rules, arguments.corpus)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(report, encoding="utf-8")


if __name__ == "__main__":
    main()
