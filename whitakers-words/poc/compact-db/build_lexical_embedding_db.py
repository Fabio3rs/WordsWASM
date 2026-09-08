#!/usr/bin/env python3

"""Build the compact provenance database used by lexical embedding jobs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import lexical_embedding_db as lexical


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="lexical comparison JSONL or JSONL.GZ")
    parser.add_argument("--input-report", type=Path, help="optional comparison report with source manifest")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    report = lexical.build_database(arguments.input, arguments.output, arguments.input_report)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"wrote {report['counts'].get('entries', 0)} entries and "
        f"{report['counts'].get('documents', 0)} semantic documents to {arguments.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
