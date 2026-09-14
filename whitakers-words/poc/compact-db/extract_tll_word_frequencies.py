#!/usr/bin/env python3

"""Extract deterministic word frequencies from the active TLL NLP selection."""

from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import io
import json
import os
import re
import sqlite3
import tempfile
import unicodedata
from collections import Counter
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator, TextIO


SCHEMA = "whitakers-words.tll-word-frequencies.v1"
REPORT_SCHEMA = "whitakers-words.tll-word-frequency-report.v1"
WORD_RE = re.compile(r"[^\W\d_]+(?:['’][^\W\d_]+)*", re.UNICODE)
FIELDNAMES = (
    "form",
    "occurrences",
    "artifact_count",
    "initial_upper_occurrences",
    "all_upper_occurrences",
    "followed_by_period_occurrences",
    "variants_json",
)


class FrequencyError(RuntimeError):
    """Raised when the TLL snapshot does not satisfy the extraction contract."""


@dataclass(slots=True)
class FormStats:
    occurrences: int = 0
    artifact_count: int = 0
    initial_upper_occurrences: int = 0
    all_upper_occurrences: int = 0
    followed_by_period_occurrences: int = 0
    variants: Counter[str] = field(default_factory=Counter)


def normalize_form(value: str) -> str:
    return unicodedata.normalize("NFC", value.casefold())


def sqlite_uri(path: Path) -> str:
    return f"file:{path.resolve().as_posix()}?mode=ro&immutable=1"


def readonly_connection(path: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(sqlite_uri(path), uri=True)
    connection.execute("PRAGMA query_only=ON")
    connection.row_factory = sqlite3.Row
    if connection.execute("PRAGMA query_only").fetchone()[0] != 1:
        connection.close()
        raise FrequencyError("could not enable SQLite query_only mode")
    return connection


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


@contextmanager
def atomic_text_output(path: Path) -> Iterator[TextIO]:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    raw = os.fdopen(descriptor, "wb")
    try:
        if path.suffix == ".gz":
            binary = gzip.GzipFile(
                filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0
            )
            stream = io.TextIOWrapper(binary, encoding="utf-8", newline="")
        else:
            stream = io.TextIOWrapper(raw, encoding="utf-8", newline="")
        try:
            yield stream
            stream.flush()
        finally:
            stream.close()
            if not raw.closed:
                raw.close()
        temporary.replace(path)
    except BaseException:
        if not raw.closed:
            raw.close()
        temporary.unlink(missing_ok=True)
        raise


def active_selection_run(connection: sqlite3.Connection) -> sqlite3.Row:
    rows = connection.execute(
        "SELECT id,pipeline_name,pipeline_version,config_sha256,config_json,"
        "started_at,finished_at FROM processing_run "
        "WHERE run_type='nlp_prepare' AND status='completed' AND is_active=1 "
        "ORDER BY id"
    ).fetchall()
    if len(rows) != 1:
        raise FrequencyError(
            f"expected exactly one active completed nlp_prepare run, found {len(rows)}"
        )
    return rows[0]


def extract_frequencies(
    connection: sqlite3.Connection, run_id: int
) -> tuple[dict[str, FormStats], dict[str, int]]:
    query = """
        SELECT s.source_artifact_id,u.normalized_text
          FROM nlp_selection s
          JOIN text_unit u ON u.id=s.text_unit_id
         WHERE s.processing_run_id=?
           AND s.decision='include'
           AND u.is_canonical=1
         ORDER BY s.source_artifact_id,s.sequence_no,s.id
    """
    forms: dict[str, FormStats] = {}
    selected_units = 0
    selected_artifacts = 0
    current_artifact: int | None = None
    artifact_forms: set[str] = set()

    def finish_artifact() -> None:
        nonlocal selected_artifacts
        if current_artifact is None:
            return
        selected_artifacts += 1
        for form in artifact_forms:
            forms[form].artifact_count += 1
        artifact_forms.clear()

    for row in connection.execute(query, (run_id,)):
        artifact_id = int(row["source_artifact_id"])
        if artifact_id != current_artifact:
            finish_artifact()
            current_artifact = artifact_id
        selected_units += 1
        text = str(row["normalized_text"])
        for match in WORD_RE.finditer(text):
            surface = match.group(0)
            form = normalize_form(surface)
            stats = forms.setdefault(form, FormStats())
            stats.occurrences += 1
            stats.initial_upper_occurrences += int(surface[0].isupper())
            stats.all_upper_occurrences += int(surface.isupper())
            stats.followed_by_period_occurrences += int(
                match.end() < len(text) and text[match.end()] == "."
            )
            stats.variants[surface] += 1
            artifact_forms.add(form)
    finish_artifact()
    return forms, {
        "selected_units": selected_units,
        "selected_artifacts": selected_artifacts,
        "tokens": sum(item.occurrences for item in forms.values()),
        "distinct_forms": len(forms),
    }


def ordered_variants(variants: Counter[str]) -> list[dict[str, object]]:
    return [
        {"surface": surface, "occurrences": count}
        for surface, count in sorted(
            variants.items(), key=lambda item: (-item[1], item[0])
        )
    ]


def write_frequencies(path: Path, forms: dict[str, FormStats]) -> None:
    with atomic_text_output(path) as stream:
        writer = csv.DictWriter(
            stream, fieldnames=FIELDNAMES, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        for form, stats in sorted(
            forms.items(), key=lambda item: (-item[1].occurrences, item[0])
        ):
            writer.writerow(
                {
                    "form": form,
                    "occurrences": stats.occurrences,
                    "artifact_count": stats.artifact_count,
                    "initial_upper_occurrences": stats.initial_upper_occurrences,
                    "all_upper_occurrences": stats.all_upper_occurrences,
                    "followed_by_period_occurrences": (
                        stats.followed_by_period_occurrences
                    ),
                    "variants_json": json.dumps(
                        ordered_variants(stats.variants),
                        ensure_ascii=False,
                        separators=(",", ":"),
                    ),
                }
            )


def write_json(path: Path, value: object) -> None:
    with atomic_text_output(path) as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tll_database", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    source_stat = arguments.tll_database.stat()
    connection = readonly_connection(arguments.tll_database)
    try:
        run = active_selection_run(connection)
        user_version = int(connection.execute("PRAGMA user_version").fetchone()[0])
        forms, counts = extract_frequencies(connection, int(run["id"]))
    finally:
        connection.close()

    write_frequencies(arguments.output, forms)
    report = {
        "schema": REPORT_SCHEMA,
        "frequency_schema": SCHEMA,
        "policy": {
            "source": "active completed nlp_prepare selection",
            "selection_decision": "include",
            "text_column": "text_unit.normalized_text",
            "normalization": "NFC after Unicode casefold",
            "token_pattern": WORD_RE.pattern,
            "sqlite": "mode=ro&immutable=1; PRAGMA query_only=ON",
        },
        "counts": counts,
        "selection_run": dict(run),
        "source": {
            "path": str(arguments.tll_database.resolve()),
            "size_bytes": source_stat.st_size,
            "mtime_ns": source_stat.st_mtime_ns,
            "user_version": user_version,
            "sha256": file_digest(arguments.tll_database),
        },
        "output": {
            "path": str(arguments.output),
            "sha256": file_digest(arguments.output),
        },
    }
    write_json(arguments.report, report)
    print(
        f"wrote {counts['distinct_forms']} forms / {counts['tokens']} occurrences "
        f"to {arguments.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
