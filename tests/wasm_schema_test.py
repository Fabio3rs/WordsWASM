#!/usr/bin/env python3
"""Validate real high-level WASM documents against the browser v3 schemas."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess

import jsonschema


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--module", type=pathlib.Path, required=True)
    parser.add_argument("--database", type=pathlib.Path, required=True)
    parser.add_argument("--dataset-id", required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    search_path = (root / "schemas/browser-search-v3.schema.json").resolve()
    analysis_path = (root / "schemas/browser-analysis-v3.schema.json").resolve()
    search_schema = json.loads(search_path.read_text(encoding="utf-8"))
    analysis_schema = json.loads(analysis_path.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(search_schema)
    jsonschema.Draft202012Validator.check_schema(analysis_schema)

    completed = subprocess.run(
        [
            "node",
            str(root / "tests/wasm_contract_fixture.mjs"),
            str(args.module.resolve()),
            str(args.database.resolve()),
            args.dataset_id,
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    documents = json.loads(completed.stdout)
    store = {search_path.as_uri(): search_schema}
    search_validator = jsonschema.Draft202012Validator(search_schema)
    analysis_validator = jsonschema.Draft202012Validator(
        analysis_schema,
        resolver=jsonschema.RefResolver(
            base_uri=analysis_path.as_uri(),
            referrer=analysis_schema,
            store=store,
        ),
    )
    for document in documents["search"]:
        search_validator.validate(document)
    for document in documents["analysis"]:
        analysis_validator.validate(document)

    print(json.dumps({
        "ok": True,
        "analysisDocuments": len(documents["analysis"]),
        "searchDocuments": len(documents["search"]),
    }))


if __name__ == "__main__":
    main()
