#!/usr/bin/env python3
"""Validate curated grammar examples against canonical OCR page records."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import unicodedata
from typing import Any


def folded(value: str) -> str:
    value = value.replace("Æ", "Ae").replace("æ", "ae")
    value = value.replace("Œ", "Oe").replace("œ", "oe")
    value = "".join(
        character
        for character in unicodedata.normalize("NFD", value)
        if unicodedata.category(character) != "Mn"
    )
    return re.sub(r"[^a-z0-9]+", " ", value.casefold()).strip()


def load_pages(path: pathlib.Path) -> dict[str, dict[str, Any]]:
    pages: dict[str, dict[str, Any]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            page = json.loads(line)
            pages[page["unit_id"]] = page
    return pages


def validate(catalog: dict[str, Any], pages: dict[str, dict[str, Any]]) -> list[str]:
    errors: list[str] = []
    resolved_sources: dict[str, str] = {}
    for source_id, expected in catalog["sources"].items():
        page = pages.get(expected["unitId"])
        if page is None:
            errors.append(f"{source_id}: unit not found")
            continue
        blocks = {
            block["local_id"]: block for block in page["extraction"]["blocks"]
        }
        block = blocks.get(expected["blockId"])
        if block is None:
            errors.append(f"{source_id}: block not found")
            continue
        observed = {
            "printedPage": page["pagination"]["expected_printed_page"],
            "recordStatus": page["status"],
            "blockConfidence": block["confidence"],
        }
        for field, value in observed.items():
            if expected[field] != value:
                errors.append(
                    f"{source_id}: {field} expected {expected[field]!r}, got {value!r}"
                )
        resolved_sources[source_id] = folded(block["plain_text"])

    ids: set[str] = set()
    for example in catalog["examples"]:
        example_id = example["id"]
        if example_id in ids:
            errors.append(f"{example_id}: duplicate id")
        ids.add(example_id)
        if not re.fullmatch(r"[a-z0-9-]+", example_id):
            errors.append(f"{example_id}: id is not stable ASCII kebab-case")
        source = resolved_sources.get(example["source"])
        if source is None:
            errors.append(f"{example_id}: unresolved source")
        elif folded(example["sourceText"]) not in source:
            errors.append(f"{example_id}: sourceText not found in canonical block")
        if example["suite"] not in {"S0", "S1", "S2", "S3", "S4"}:
            errors.append(f"{example_id}: invalid suite")
        if example["mode"] not in {"complete-clause", "fragment"}:
            errors.append(f"{example_id}: invalid mode")
    return errors


def validate_promotions(
    catalog: dict[str, Any], fixture_document: dict[str, Any]
) -> tuple[list[str], int]:
    errors: list[str] = []
    fixtures = {fixture["id"]: fixture for fixture in fixture_document["fixtures"]}
    examples = {example["id"]: example for example in catalog["examples"]}
    promoted = 0
    for example in catalog["examples"]:
        status = example.get("annotationStatus")
        fixture_id = example.get("fixtureId")
        if status is None and fixture_id is None:
            continue
        if status != "promoted-to-structural-gold" or not fixture_id:
            errors.append(f"{example['id']}: incomplete promotion metadata")
            continue
        promoted += 1
        fixture = fixtures.get(fixture_id)
        if fixture is None:
            errors.append(f"{example['id']}: promoted fixture not found")
            continue
        annotation = fixture.get("annotation", {})
        source = annotation.get("source", {})
        expected_source = catalog["sources"][example["source"]]
        checks = {
            "annotation.status": annotation.get("status") == "verified-didactic",
            "source.catalogId": source.get("catalogId") == example["id"],
            "source.unitId": source.get("unitId") == expected_source["unitId"],
            "source.blockId": source.get("blockId") == expected_source["blockId"],
            "source.printedPage": source.get("printedPage")
            == expected_source["printedPage"],
            "source.sourceText": folded(source.get("sourceText", ""))
            == folded(example["sourceText"]),
            "fixture.text": folded(fixture["text"]) == folded(example["text"]),
            "gold.morphology": bool(fixture.get("gold", {}).get("morphology")),
            "gold.dependencies": bool(
                fixture.get("gold", {}).get("dependencies")
            ),
        }
        for field, valid in checks.items():
            if not valid:
                errors.append(f"{example['id']}: promotion mismatch in {field}")

    for fixture in fixtures.values():
        annotation = fixture.get("annotation")
        if not annotation:
            continue
        catalog_id = annotation.get("source", {}).get("catalogId")
        example = examples.get(catalog_id)
        if example is None:
            errors.append(f"{fixture['id']}: annotated catalogId not found")
        elif example.get("fixtureId") != fixture["id"]:
            errors.append(f"{fixture['id']}: catalog promotion is not reciprocal")
    return errors, promoted


def audit_morphology(
    catalog: dict[str, Any], parser_path: pathlib.Path, database: pathlib.Path | None
) -> tuple[int, list[tuple[str, list[str]]]]:
    failures: list[tuple[str, list[str]]] = []
    for example in catalog["examples"]:
        command = [
            str(parser_path),
            "--strategy",
            "morphology",
            "--text",
            example["text"],
        ]
        if database is not None:
            command.extend(["--database", str(database)])
        completed = subprocess.run(
            command, check=True, capture_output=True, text=True
        )
        result = json.loads(completed.stdout)
        if result["status"] != "ok":
            failures.append((example["id"], result["diagnostics"]))
    return len(catalog["examples"]) - len(failures), failures


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", type=pathlib.Path)
    parser.add_argument("pages_jsonl", type=pathlib.Path)
    parser.add_argument("--parser", type=pathlib.Path)
    parser.add_argument("--database", type=pathlib.Path)
    parser.add_argument("--fixtures", type=pathlib.Path)
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    pages = load_pages(args.pages_jsonl)
    errors = validate(catalog, pages)
    if errors:
        for error in errors:
            print(error)
        raise SystemExit(1)
    print(
        f"source audit: {len(catalog['examples'])} examples, "
        f"{len(catalog['sources'])} source blocks passed"
    )
    if args.fixtures:
        fixture_document = json.loads(args.fixtures.read_text(encoding="utf-8"))
        promotion_errors, promoted = validate_promotions(
            catalog, fixture_document
        )
        if promotion_errors:
            for error in promotion_errors:
                print(error)
            raise SystemExit(1)
        print(f"promotion audit: {promoted} structural fixtures passed")
    if args.parser:
        passed, failures = audit_morphology(catalog, args.parser, args.database)
        print(f"morphology audit: {passed}/{len(catalog['examples'])} passed")
        for example_id, diagnostics in failures:
            print(f"  {example_id}: {', '.join(diagnostics)}")


if __name__ == "__main__":
    main()
