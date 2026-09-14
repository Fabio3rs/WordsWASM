#!/usr/bin/env python3

"""Rank TLL forms unknown to WORDS and enrich them with lexical evidence."""

from __future__ import annotations

import argparse
import csv
import gzip
import itertools
import json
import sqlite3
import subprocess
import sys
import tempfile
import threading
import unicodedata
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Iterator

import audit_lexical_expansion as expansion
import suggest_quantity_evidence as quantity
from extract_tll_word_frequencies import (
    FIELDNAMES as FREQUENCY_FIELDS,
    atomic_text_output,
    file_digest,
    readonly_connection,
    write_json,
)


REPORT_SCHEMA = "whitakers-words.tll-corpus-coverage-report.v1"
CANDIDATE_SCHEMA = "whitakers-words.tll-lexeme-priority.v1"
QUANTITY_MARKS = frozenset({"\N{COMBINING MACRON}", "\N{COMBINING BREVE}"})
COVERAGE_FIELDS = FREQUENCY_FIELDS + (
    "words_status",
    "coverage_status",
    "lexeme_ids_json",
    "diagnostic_codes_json",
    "quantity_stripped_form",
    "quantity_stripped_status",
    "two_words_suggestions_json",
    "queue",
    "candidate_groups_json",
    "evidence_sources_json",
)
TOP_FIELDS = (
    "queue",
    "kind",
    "key",
    "part_of_speech",
    "occurrences",
    "exclusive_occurrences",
    "ambiguous_occurrences",
    "artifact_count",
    "support",
    "top_forms",
)


class RankingError(RuntimeError):
    """Raised when an input or subprocess violates the ranking contract."""


@dataclass(frozen=True, slots=True)
class Frequency:
    form: str
    occurrences: int
    artifact_count: int
    initial_upper_occurrences: int
    all_upper_occurrences: int
    followed_by_period_occurrences: int
    variants_json: str


@dataclass(frozen=True, slots=True)
class WordsResult:
    status: str
    lexeme_ids: tuple[int, ...]
    diagnostic_codes: tuple[str, ...]
    two_words_suggestions: tuple[dict[str, object], ...]


@dataclass(slots=True)
class CandidateStats:
    upper_bound: int = 0
    exclusive: int = 0
    ambiguous: int = 0
    forms: list[tuple[str, int, bool]] = field(default_factory=list)


def open_text_input(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", newline="")
    return path.open("r", encoding="utf-8", newline="")


def load_frequencies(path: Path) -> list[Frequency]:
    result: list[Frequency] = []
    with open_text_input(path) as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if tuple(reader.fieldnames or ()) != FREQUENCY_FIELDS:
            raise RankingError("frequency TSV has an unsupported header")
        for row in reader:
            form = str(row["form"])
            if not form or "\n" in form or "\r" in form:
                raise RankingError("frequency TSV contains an invalid form")
            result.append(
                Frequency(
                    form=form,
                    occurrences=int(row["occurrences"]),
                    artifact_count=int(row["artifact_count"]),
                    initial_upper_occurrences=int(row["initial_upper_occurrences"]),
                    all_upper_occurrences=int(row["all_upper_occurrences"]),
                    followed_by_period_occurrences=int(
                        row["followed_by_period_occurrences"]
                    ),
                    variants_json=str(row["variants_json"]),
                )
            )
    if len({item.form for item in result}) != len(result):
        raise RankingError("frequency TSV contains duplicate normalized forms")
    return result


def compact_json(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def words_command(
    executable: Path, database: Path, dataset_id: str, *, two_words: bool
) -> list[str]:
    command = [
        str(executable),
        "--database",
        str(database),
        "--dataset-id",
        dataset_id,
        "--format",
        "search-v3",
    ]
    if two_words:
        command.append("--two-words=legacy")
    command.append("--batch-json-lines")
    return command


def classify_with_words(
    forms: list[str],
    executable: Path,
    database: Path,
    dataset_id: str,
    *,
    two_words: bool = False,
) -> dict[str, WordsResult]:
    if not forms:
        return {}
    writer_errors: list[BaseException] = []
    with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as error_stream:
        process = subprocess.Popen(
            words_command(executable, database, dataset_id, two_words=two_words),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=error_stream,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        assert process.stdin is not None
        assert process.stdout is not None

        def produce() -> None:
            try:
                for form in forms:
                    process.stdin.write(form + "\n")
                process.stdin.close()
            except BaseException as error:  # surfaced on the consumer thread
                writer_errors.append(error)

        producer = threading.Thread(target=produce, name="words-cli-input")
        producer.start()
        results: dict[str, WordsResult] = {}
        try:
            for position, expected in enumerate(forms, start=1):
                line = process.stdout.readline()
                if not line:
                    raise RankingError(
                        f"words_cli stopped before returning a result for {expected!r}"
                    )
                try:
                    document = json.loads(line)
                except json.JSONDecodeError as error:
                    raise RankingError(f"words_cli emitted invalid JSON: {error}") from error
                if document.get("schema") != "whitakers-words.search":
                    raise RankingError("words_cli returned an unexpected schema")
                query = document.get("query", {})
                if query.get("text") != expected:
                    raise RankingError(
                        f"words_cli result order mismatch: expected {expected!r}, "
                        f"found {query.get('text')!r}"
                    )
                status = str(document.get("status"))
                if status not in {"analyzed", "unknown", "error"}:
                    raise RankingError(f"words_cli returned invalid status {status!r}")
                lexeme_ids = tuple(
                    sorted(
                        {
                            int(hit["lexemeId"])
                            for hit in document.get("hits", [])
                            if hit.get("lexemeId") is not None
                        }
                    )
                )
                diagnostic_codes = tuple(
                    sorted(
                        {
                            str(item.get("code"))
                            for item in document.get("diagnostics", [])
                            if item.get("code")
                        }
                    )
                )
                suggestions = []
                for suggestion in document.get("suggestions", []):
                    if suggestion.get("method") != "two-words":
                        continue
                    segments = []
                    for segment in suggestion.get("segments", []):
                        segments.append(
                            {
                                "text": str(segment.get("text", "")),
                                "lexeme_ids": sorted(
                                    {
                                        int(hit["lexemeId"])
                                        for hit in segment.get("hits", [])
                                        if hit.get("lexemeId") is not None
                                    }
                                ),
                            }
                        )
                    suggestions.append(
                        {
                            "split_at": int(suggestion["splitAt"]),
                            "classification": str(suggestion.get("classification", "")),
                            "segments": segments,
                        }
                    )
                results[expected] = WordsResult(
                    status, lexeme_ids, diagnostic_codes, tuple(suggestions)
                )
                if position % 25_000 == 0:
                    print(
                        f"words_cli: classified {position}/{len(forms)} forms",
                        file=sys.stderr,
                        flush=True,
                    )
            if process.stdout.readline():
                raise RankingError("words_cli returned more results than inputs")
        except BaseException:
            process.terminate()
            producer.join()
            process.wait()
            process.stdout.close()
            raise
        producer.join()
        return_code = process.wait()
        process.stdout.close()
        error_stream.seek(0)
        errors = error_stream.read().strip()
        if writer_errors:
            raise RankingError(f"failed to feed words_cli: {writer_errors[0]}")
        if return_code != 0:
            raise RankingError(
                f"words_cli exited with {return_code}: {errors or '(no stderr)'}"
            )
    return results


def strip_quantity_marks(value: str) -> str:
    decomposed = unicodedata.normalize("NFD", value)
    stripped = "".join(char for char in decomposed if char not in QUANTITY_MARKS)
    return unicodedata.normalize("NFC", stripped)


def ascii_lookup(value: str) -> str | None:
    expanded = value.replace("æ", "ae").replace("œ", "oe")
    decomposed = unicodedata.normalize("NFKD", expanded.casefold())
    result = "".join(
        char for char in decomposed if unicodedata.combining(char) == 0
    )
    return result if result and all("a" <= char <= "z" for char in result) else None


def group_key(candidate: dict[str, Any]) -> tuple[str, str, bool]:
    return (
        str(candidate["ascii_lemma"]),
        str(candidate["part_of_speech"]),
        bool(candidate["proper"]),
    )


def group_record(key: tuple[str, str, bool]) -> dict[str, object]:
    return {"ascii_lemma": key[0], "part_of_speech": key[1], "proper": key[2]}


def lexical_candidates(arguments: argparse.Namespace):
    entries: Iterable[quantity.DictionaryEntry] = quantity.read_dictionary_entries(
        arguments.superdb, arguments.sources or sorted(quantity.SQLITE_SOURCE_NAMES)
    )
    if arguments.collatinus_data is not None:
        entries = itertools.chain(
            entries,
            quantity.read_collatinus_entries(
                arguments.collatinus_data,
                include_extended=arguments.collatinus_extended,
            ),
        )
    if arguments.latin_german is not None:
        entries = itertools.chain(
            entries, quantity.read_latin_german_entries(arguments.latin_german)
        )
    if arguments.faria_v3 is not None:
        entries = itertools.chain(
            entries, expansion.read_faria_v3_entries(arguments.faria_v3)
        )
    return expansion.audit(
        arguments.dictionary,
        entries,
        maximum_ending=arguments.maximum_ending,
        inflections=arguments.dictionary.parent / "INFLECTS.SEC",
        latin_german_database=arguments.latin_german,
    )


def chunks(values: list[str], size: int = 400) -> Iterator[list[str]]:
    for offset in range(0, len(values), size):
        yield values[offset : offset + size]


def external_form_references(
    lookups: list[str], arguments: argparse.Namespace
) -> dict[str, set[tuple[str, str]]]:
    references: dict[str, set[tuple[str, str]]] = defaultdict(set)
    sources = arguments.sources or sorted(quantity.SQLITE_SOURCE_NAMES)
    connection = readonly_connection(arguments.superdb)
    try:
        for batch in chunks(lookups):
            source_marks = ",".join("?" for _ in sources)
            form_marks = ",".join("?" for _ in batch)
            query = f"""
                SELECT s.name,e.source_entry_id,f.form_norm
                  FROM entry_form f
                  JOIN entry e ON e.id=f.entry_id
                  JOIN source s ON s.id=e.source_id
                 WHERE s.name IN ({source_marks}) AND f.form_norm IN ({form_marks})
                 ORDER BY f.form_norm,s.name,e.source_entry_id
            """
            for row in connection.execute(query, (*sources, *batch)):
                evidence_id = quantity.SOURCE_EVIDENCE_IDS[str(row["name"])]
                references[str(row["form_norm"])].add(
                    (evidence_id, str(row["source_entry_id"]))
                )
    finally:
        connection.close()

    if arguments.latin_german is not None:
        connection = readonly_connection(arguments.latin_german)
        try:
            for batch in chunks(lookups):
                marks = ",".join("?" for _ in batch)
                for row in connection.execute(
                    f"SELECT form_norm,vok_id FROM FORM WHERE form_norm IN ({marks}) "
                    "ORDER BY form_norm,vok_id",
                    batch,
                ):
                    references[str(row["form_norm"])].add(
                        (
                            quantity.SOURCE_EVIDENCE_IDS["latin_german"],
                            str(row["vok_id"]),
                        )
                    )
        finally:
            connection.close()

    if arguments.faria_v3 is not None:
        connection = readonly_connection(arguments.faria_v3)
        try:
            for batch in chunks(lookups):
                marks = ",".join("?" for _ in batch)
                for row in connection.execute(
                    f"SELECT headword_search,entry_id FROM entry "
                    f"WHERE entry_kind='lexical_entry' "
                    f"AND editorial_status='publishable' "
                    f"AND headword_search IN ({marks}) "
                    "ORDER BY headword_search,sort_order",
                    batch,
                ):
                    references[str(row["headword_search"])].add(
                        (
                            quantity.SOURCE_EVIDENCE_IDS["faria_v3"],
                            str(row["entry_id"]),
                        )
                    )
        finally:
            connection.close()
    return references


def evidence_maps(candidates: Iterable[dict[str, Any]]):
    by_key: dict[tuple[str, str, bool], dict[str, Any]] = {}
    by_reference: dict[tuple[str, str], set[tuple[str, str, bool]]] = defaultdict(set)
    by_lemma: dict[str, set[tuple[str, str, bool]]] = defaultdict(set)
    for candidate in candidates:
        key = group_key(candidate)
        by_key[key] = candidate
        by_lemma[key[0]].add(key)
        for witness in candidate["witnesses"]:
            by_reference[
                (str(witness["source"]), str(witness["source_entry_id"]))
            ].add(key)
    return by_key, by_reference, by_lemma


def queue_for(
    frequency: Frequency,
    candidates: set[tuple[str, str, bool]],
    stripped_status: str | None,
    has_two_words_suggestion: bool = False,
) -> str:
    if has_two_words_suggestion:
        return "covered_two_words"
    if stripped_status == "analyzed":
        return "quantity_or_orthography"
    lookup = ascii_lookup(frequency.form)
    period_ratio = frequency.followed_by_period_occurrences / frequency.occurrences
    if lookup is not None and (
        len(lookup) <= 2
        or (len(lookup) <= 4 and period_ratio >= 0.25)
        or frequency.all_upper_occurrences == frequency.occurrences
    ):
        return "abbreviation_or_editorial"
    if len(candidates) == 1:
        if all(key[2] for key in candidates):
            return "proper_name"
        return "common_lexeme"
    if frequency.initial_upper_occurrences / frequency.occurrences >= 0.8:
        return "proper_name"
    if candidates:
        if all(key[2] for key in candidates):
            return "proper_name"
        return "common_lexeme"
    return "unresolved"


def input_manifest(arguments: argparse.Namespace) -> list[dict[str, object]]:
    paths = {
        "frequencies": arguments.frequencies,
        "frequency_report": arguments.frequency_report,
        "words_cli": arguments.words_cli,
        "words_database": arguments.words_database,
        "words_dictionary": arguments.dictionary,
        "superdb": arguments.superdb,
        "latin_german": arguments.latin_german,
        "faria_v3": arguments.faria_v3,
    }
    result = []
    for name, path in paths.items():
        if path is None:
            continue
        result.append(
            {
                "name": name,
                "path": str(path.resolve()),
                "size_bytes": path.stat().st_size,
                "sha256": file_digest(path),
            }
        )
    if arguments.collatinus_data is not None:
        for name in ("modeles.la", "lemmes.la", "lem_ext.la"):
            path = arguments.collatinus_data / name
            if path.is_file() and (name != "lem_ext.la" or arguments.collatinus_extended):
                result.append(
                    {
                        "name": f"collatinus_{name}",
                        "path": str(path.resolve()),
                        "size_bytes": path.stat().st_size,
                        "sha256": file_digest(path),
                    }
                )
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


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frequencies", type=Path)
    parser.add_argument("--frequency-report", type=Path, required=True)
    parser.add_argument("dictionary", type=Path, help="Whitaker DICTFILE.GEN")
    parser.add_argument("superdb", type=Path, help="read-only unified dictionary index")
    parser.add_argument("--words-cli", type=Path, required=True)
    parser.add_argument("--words-database", type=Path, required=True)
    parser.add_argument(
        "--source",
        action="append",
        choices=sorted(quantity.SQLITE_SOURCE_NAMES),
        dest="sources",
    )
    parser.add_argument("--collatinus-data", type=Path)
    parser.add_argument("--collatinus-extended", action="store_true")
    parser.add_argument("--latin-german", type=Path)
    parser.add_argument("--faria-v3", type=Path)
    parser.add_argument("--maximum-ending", type=int, default=6)
    parser.add_argument("--top", type=int, default=1000)
    parser.add_argument("--output-directory", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.collatinus_extended and arguments.collatinus_data is None:
        raise RankingError("--collatinus-extended requires --collatinus-data")
    if arguments.maximum_ending < 0 or arguments.maximum_ending > quantity.STEM_SIZE:
        raise RankingError("--maximum-ending must be in 0..18")
    if arguments.top <= 0:
        raise RankingError("--top must be positive")

    frequencies = load_frequencies(arguments.frequencies)
    frequency_report = json.loads(arguments.frequency_report.read_text(encoding="utf-8"))
    if frequency_report.get("schema") != "whitakers-words.tll-word-frequency-report.v1":
        raise RankingError("frequency report has an unsupported schema")
    expected_frequency_hash = frequency_report.get("output", {}).get("sha256")
    if expected_frequency_hash != file_digest(arguments.frequencies):
        raise RankingError("frequency report does not describe the supplied TSV")
    print(f"loaded {len(frequencies)} corpus forms", file=sys.stderr, flush=True)
    dataset_id = file_digest(arguments.words_database)
    forms = [item.form for item in frequencies]
    words_results = classify_with_words(
        forms, arguments.words_cli, arguments.words_database, dataset_id
    )
    unknown_forms = [
        item.form for item in frequencies if words_results[item.form].status == "unknown"
    ]
    print(
        f"probing two-word suggestions for {len(unknown_forms)} unknown forms",
        file=sys.stderr,
        flush=True,
    )
    two_words_results = classify_with_words(
        unknown_forms,
        arguments.words_cli,
        arguments.words_database,
        dataset_id,
        two_words=True,
    )
    stripped_forms = sorted(
        {
            stripped
            for item in frequencies
            if words_results[item.form].status in {"unknown", "error"}
            and (stripped := strip_quantity_marks(item.form)) != item.form
        }
    )
    stripped_results = classify_with_words(
        stripped_forms, arguments.words_cli, arguments.words_database, dataset_id
    )

    print("building structural lexical audit", file=sys.stderr, flush=True)
    lexical_report, raw_candidates = lexical_candidates(arguments)
    print(
        f"loaded {len(raw_candidates)} structurally unmatched lexical groups",
        file=sys.stderr,
        flush=True,
    )
    candidates, by_reference, by_lemma = evidence_maps(raw_candidates)
    unknown_lookups = sorted(
        {
            lookup
            for item in frequencies
            if words_results[item.form].status == "unknown"
            and not two_words_results[item.form].two_words_suggestions
            and (lookup := ascii_lookup(item.form)) is not None
        }
    )
    references = external_form_references(unknown_lookups, arguments)
    print(
        f"enriched {len(references)} external form keys", file=sys.stderr, flush=True
    )

    candidate_stats: dict[tuple[str, str, bool], CandidateStats] = defaultdict(
        CandidateStats
    )
    coverage_rows: list[dict[str, object]] = []
    unknown_rows: list[dict[str, object]] = []
    error_rows: list[dict[str, object]] = []
    queue_counts: Counter[str] = Counter()
    queue_occurrences: Counter[str] = Counter()
    status_counts: Counter[str] = Counter()
    status_occurrences: Counter[str] = Counter()
    raw_status_counts: Counter[str] = Counter()
    raw_status_occurrences: Counter[str] = Counter()
    diagnostic_counts: Counter[str] = Counter()
    diagnostic_occurrences: Counter[str] = Counter()

    for frequency in frequencies:
        result = words_results[frequency.form]
        two_words = (
            two_words_results[frequency.form].two_words_suggestions
            if result.status == "unknown"
            else ()
        )
        coverage_status = "analyzed_two_words" if two_words else result.status
        raw_status_counts[result.status] += 1
        raw_status_occurrences[result.status] += frequency.occurrences
        status_counts[coverage_status] += 1
        status_occurrences[coverage_status] += frequency.occurrences
        for code in result.diagnostic_codes:
            diagnostic_counts[code] += 1
            diagnostic_occurrences[code] += frequency.occurrences
        lookup = ascii_lookup(frequency.form)
        matched: set[tuple[str, str, bool]] = set()
        source_names: set[str] = set()
        if coverage_status == "unknown" and lookup is not None:
            matched.update(by_lemma.get(lookup, set()))
            for reference in references.get(lookup, set()):
                source_names.add(reference[0])
                matched.update(by_reference.get(reference, set()))
        stripped = strip_quantity_marks(frequency.form)
        stripped_result = stripped_results.get(stripped) if stripped != frequency.form else None
        queue = (
            queue_for(
                frequency,
                matched,
                stripped_result.status if stripped_result is not None else None,
                bool(two_words),
            )
            if result.status == "unknown"
            else "engine_error" if result.status == "error" else "covered"
        )
        if coverage_status == "unknown":
            queue_counts[queue] += 1
            queue_occurrences[queue] += frequency.occurrences
            if queue in {"common_lexeme", "proper_name"}:
                for key in matched:
                    stats = candidate_stats[key]
                    stats.upper_bound += frequency.occurrences
                    if len(matched) == 1:
                        stats.exclusive += frequency.occurrences
                    else:
                        stats.ambiguous += frequency.occurrences
                    stats.forms.append(
                        (frequency.form, frequency.occurrences, len(matched) == 1)
                    )
        row = {
            "form": frequency.form,
            "occurrences": frequency.occurrences,
            "artifact_count": frequency.artifact_count,
            "initial_upper_occurrences": frequency.initial_upper_occurrences,
            "all_upper_occurrences": frequency.all_upper_occurrences,
            "followed_by_period_occurrences": frequency.followed_by_period_occurrences,
            "variants_json": frequency.variants_json,
            "words_status": result.status,
            "coverage_status": coverage_status,
            "lexeme_ids_json": compact_json(result.lexeme_ids),
            "diagnostic_codes_json": compact_json(result.diagnostic_codes),
            "quantity_stripped_form": stripped if stripped != frequency.form else "",
            "quantity_stripped_status": (
                stripped_result.status if stripped_result is not None else ""
            ),
            "two_words_suggestions_json": compact_json(two_words),
            "queue": queue,
            "candidate_groups_json": compact_json(
                [group_record(key) for key in sorted(matched)]
            ),
            "evidence_sources_json": compact_json(sorted(source_names)),
        }
        coverage_rows.append(row)
        if coverage_status == "unknown":
            unknown_rows.append(row)
        elif result.status == "error":
            error_rows.append(row)

    coverage_rows.sort(key=lambda row: (-int(row["occurrences"]), str(row["form"])))
    unknown_rows.sort(key=lambda row: (-int(row["occurrences"]), str(row["form"])))
    error_rows.sort(key=lambda row: (-int(row["occurrences"]), str(row["form"])))

    priority_candidates: list[dict[str, object]] = []
    for key, stats in candidate_stats.items():
        candidate = candidates[key]
        top_forms = sorted(stats.forms, key=lambda item: (-item[1], item[0]))[:20]
        priority_candidates.append(
            {
                "schema": CANDIDATE_SCHEMA,
                "key": group_record(key),
                "corpus": {
                    "corpus_occurrences_upper_bound": stats.upper_bound,
                    "exclusive_occurrences": stats.exclusive,
                    "ambiguous_occurrences": stats.ambiguous,
                    "top_unknown_forms": [
                        {"form": form, "occurrences": count, "exclusive": exclusive}
                        for form, count, exclusive in top_forms
                    ],
                },
                "lexical_evidence": candidate,
                "automatic_promotion_allowed": False,
            }
        )
    priority_candidates.sort(
        key=lambda row: (
            -int(row["corpus"]["exclusive_occurrences"]),  # type: ignore[index]
            -int(row["corpus"]["corpus_occurrences_upper_bound"]),  # type: ignore[index]
            str(row["key"]),
        )
    )

    top_rows: list[dict[str, object]] = []
    for queue in (
        "common_lexeme",
        "proper_name",
        "quantity_or_orthography",
        "abbreviation_or_editorial",
        "unresolved",
    ):
        if queue in {"common_lexeme", "proper_name"}:
            selected = [
                row
                for row in priority_candidates
                if ("proper_name" if row["key"]["proper"] else "common_lexeme")  # type: ignore[index]
                == queue
            ][: arguments.top]
            for row in selected:
                key = row["key"]  # type: ignore[assignment]
                corpus = row["corpus"]  # type: ignore[assignment]
                evidence = row["lexical_evidence"]  # type: ignore[assignment]
                top_rows.append(
                    {
                        "queue": queue,
                        "kind": "lexical_group",
                        "key": key["ascii_lemma"],
                        "part_of_speech": key["part_of_speech"],
                        "occurrences": corpus["corpus_occurrences_upper_bound"],
                        "exclusive_occurrences": corpus["exclusive_occurrences"],
                        "ambiguous_occurrences": corpus["ambiguous_occurrences"],
                        "artifact_count": "",
                        "support": evidence["support"],
                        "top_forms": compact_json(corpus["top_unknown_forms"]),
                    }
                )
        else:
            selected = [row for row in unknown_rows if row["queue"] == queue][
                : arguments.top
            ]
            for row in selected:
                top_rows.append(
                    {
                        "queue": queue,
                        "kind": "surface",
                        "key": row["form"],
                        "part_of_speech": "",
                        "occurrences": row["occurrences"],
                        "exclusive_occurrences": "",
                        "ambiguous_occurrences": "",
                        "artifact_count": row["artifact_count"],
                        "support": "",
                        "top_forms": row["variants_json"],
                    }
                )

    output = arguments.output_directory
    print("writing final artifacts", file=sys.stderr, flush=True)
    output.mkdir(parents=True, exist_ok=True)
    paths = {
        "coverage": output / "tll-word-coverage.tsv.gz",
        "unknown": output / "tll-unknown-forms.tsv.gz",
        "errors": output / "tll-engine-errors.tsv.gz",
        "candidates": output / "tll-lexeme-priorities.jsonl.gz",
        "top": output / "tll-top-priorities.tsv",
        "report": output / "tll-coverage-report.json",
    }
    write_tsv(paths["coverage"], COVERAGE_FIELDS, coverage_rows)
    write_tsv(paths["unknown"], COVERAGE_FIELDS, unknown_rows)
    write_tsv(paths["errors"], COVERAGE_FIELDS, error_rows)
    write_jsonl(paths["candidates"], priority_candidates)
    write_tsv(paths["top"], TOP_FIELDS, top_rows)

    report = {
        "schema": REPORT_SCHEMA,
        "policy": {
            "coverage_authority": "words_cli search-v3 status plus complete legacy two-word analysis",
            "raw_status_field": "words_status",
            "effective_status_field": "coverage_status",
            "missing_status": "coverage_status=unknown",
            "two_words": "raw unknown with a complete --two-words=legacy suggestion is analyzed_two_words",
            "words_options": "defaults: classical-and-medieval; all mechanisms enabled",
            "lexical_candidates_are_editorial": True,
            "automatic_promotion_allowed": False,
            "ambiguous_attribution": "upper bound and ambiguous counts; never duplicated as exclusive evidence",
            "sqlite": "mode=ro&immutable=1; PRAGMA query_only=ON",
        },
        "dataset_id": dataset_id,
        "corpus": frequency_report,
        "counts": {
            "forms_by_status": dict(sorted(status_counts.items())),
            "occurrences_by_status": dict(sorted(status_occurrences.items())),
            "raw_forms_by_words_status": dict(sorted(raw_status_counts.items())),
            "raw_occurrences_by_words_status": dict(
                sorted(raw_status_occurrences.items())
            ),
            "forms_by_queue": dict(sorted(queue_counts.items())),
            "occurrences_by_queue": dict(sorted(queue_occurrences.items())),
            "candidate_groups_with_corpus_evidence": len(priority_candidates),
            "forms_total": len(frequencies),
            "occurrences_total": sum(item.occurrences for item in frequencies),
        },
        "diagnostics": {
            "forms": dict(sorted(diagnostic_counts.items())),
            "occurrences": dict(sorted(diagnostic_occurrences.items())),
        },
        "lexical_expansion": lexical_report,
        "inputs": input_manifest(arguments),
        "outputs": {
            name: {"path": str(path), "sha256": file_digest(path)}
            for name, path in paths.items()
            if name != "report"
        },
    }
    write_json(paths["report"], report)
    print(
        f"wrote {len(unknown_rows)} unknown forms and "
        f"{len(priority_candidates)} corpus-linked lexical groups to {output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
