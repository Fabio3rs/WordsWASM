#!/usr/bin/env python3

"""Shared storage and text preparation for lexical embedding tools."""

from __future__ import annotations

import gzip
import hashlib
import html
import json
import os
import re
import sqlite3
import tempfile
import unicodedata
from collections import Counter
from pathlib import Path
from typing import Any, Iterable, Iterator, TextIO


INPUT_SCHEMA = "whitakers-words.lexical-comparison-packet.v1"
DATABASE_SCHEMA = "whitakers-words.lexical-embedding-evidence.v1"
PROMPT_VERSION = "latin-lexical-sense-similarity-v1"
DEFAULT_MODEL = "qwen3-embedding:8b"
DEFAULT_OLLAMA_URL = "http://localhost:11434"
DEFAULT_INSTRUCTION = (
    "Retrieve dictionary senses in any language that describe the same sense "
    "of the same Latin headword. Distinguish homographs; topical relation or "
    "related meaning alone is not sufficient."
)

TAG = re.compile(r"<[^>]+>")
SPACE = re.compile(r"\s+")
CROSSREF_ONLY = re.compile(
    r"^(?:cf\.?|see|v\.?|vide|voir|veja|vgl\.?)\b", re.IGNORECASE
)
PURE_ALIAS_RELATIONS = frozenset(
    {"alias", "pure_alias", "orthographic_variant", "variant", "variante"}
)


class LexicalEmbeddingError(ValueError):
    """An evidence database or embedding input is unsafe or malformed."""


SCHEMA_SQL = f"""
PRAGMA foreign_keys = ON;
CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
) STRICT;
CREATE TABLE input_snapshot (
    input_kind TEXT PRIMARY KEY,
    logical_path TEXT NOT NULL,
    size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
    sha256 TEXT NOT NULL CHECK(length(sha256) = 71)
) STRICT;
CREATE TABLE source_snapshot (
    source_key TEXT PRIMARY KEY,
    source_family TEXT NOT NULL,
    language TEXT NOT NULL,
    primary_authority INTEGER NOT NULL CHECK(primary_authority IN (0, 1)),
    logical_path TEXT,
    size_bytes INTEGER CHECK(size_bytes IS NULL OR size_bytes >= 0),
    sha256 TEXT CHECK(sha256 IS NULL OR length(sha256) = 71),
    schema_signature TEXT
) STRICT;
CREATE TABLE entry (
    entry_ref TEXT PRIMARY KEY,
    source_key TEXT NOT NULL REFERENCES source_snapshot(source_key),
    source_family TEXT NOT NULL,
    source_entry_id TEXT NOT NULL,
    ascii_lemma TEXT NOT NULL,
    proper INTEGER NOT NULL CHECK(proper IN (0, 1)),
    lemma_display TEXT NOT NULL,
    homograph_number INTEGER,
    part_of_speech_raw TEXT,
    part_of_speech TEXT,
    gender_raw TEXT,
    gender TEXT,
    indeclinable INTEGER CHECK(indeclinable IS NULL OR indeclinable IN (0, 1)),
    current_words_match_ids_json TEXT NOT NULL,
    quantity_json TEXT NOT NULL,
    morphology_json TEXT NOT NULL,
    meanings_json TEXT NOT NULL,
    metadata_json TEXT NOT NULL,
    semantic_status TEXT NOT NULL CHECK(semantic_status IN ('available','missing')),
    packet_revision TEXT NOT NULL CHECK(length(packet_revision) = 71),
    content_sha256 TEXT NOT NULL CHECK(length(content_sha256) = 71),
    UNIQUE(source_key, source_entry_id)
) STRICT;
CREATE INDEX entry_bucket_idx ON entry(ascii_lemma, proper, part_of_speech);
CREATE TABLE entry_bucket (
    entry_ref TEXT NOT NULL REFERENCES entry(entry_ref) ON DELETE CASCADE,
    ascii_lemma TEXT NOT NULL,
    proper INTEGER NOT NULL CHECK(proper IN (0, 1)),
    packet_revision TEXT NOT NULL CHECK(length(packet_revision) = 71),
    PRIMARY KEY(entry_ref, ascii_lemma, proper)
) STRICT;
CREATE INDEX entry_bucket_group_idx ON entry_bucket(ascii_lemma, proper, entry_ref);
CREATE TABLE entry_form (
    entry_ref TEXT NOT NULL REFERENCES entry(entry_ref) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
    role TEXT NOT NULL CHECK(role IN ('lemma','principal_part','stem','variant')),
    form_display TEXT NOT NULL,
    form_ascii TEXT,
    source_kind TEXT,
    quantity_json TEXT NOT NULL,
    PRIMARY KEY(entry_ref, ordinal)
) STRICT;
CREATE TABLE entry_alias (
    entry_ref TEXT NOT NULL REFERENCES entry(entry_ref) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
    target_text TEXT NOT NULL,
    target_ascii TEXT,
    relation TEXT NOT NULL,
    pure_alias INTEGER NOT NULL CHECK(pure_alias IN (0, 1)),
    PRIMARY KEY(entry_ref, ordinal)
) STRICT;
CREATE INDEX entry_alias_target_idx ON entry_alias(target_ascii, pure_alias);
CREATE TABLE sense (
    sense_ref TEXT PRIMARY KEY,
    entry_ref TEXT NOT NULL REFERENCES entry(entry_ref) ON DELETE CASCADE,
    source_sense_id TEXT,
    parent_source_sense_id TEXT,
    ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
    language TEXT NOT NULL,
    label TEXT,
    definition_raw TEXT NOT NULL,
    content_sha256 TEXT NOT NULL CHECK(length(content_sha256) = 71)
) STRICT;
CREATE INDEX sense_entry_idx ON sense(entry_ref, ordinal);
CREATE TABLE semantic_document (
    document_ref TEXT PRIMARY KEY,
    entry_ref TEXT NOT NULL REFERENCES entry(entry_ref) ON DELETE CASCADE,
    sense_ref TEXT REFERENCES sense(sense_ref) ON DELETE CASCADE,
    language TEXT NOT NULL,
    quality TEXT NOT NULL CHECK(quality IN ('sense','entry_fallback','crossref_only')),
    chunk_index INTEGER NOT NULL CHECK(chunk_index >= 0),
    char_start INTEGER NOT NULL CHECK(char_start >= 0),
    char_end INTEGER NOT NULL CHECK(char_end >= char_start),
    semantic_text TEXT NOT NULL,
    text_sha256 TEXT NOT NULL CHECK(length(text_sha256) = 71)
) STRICT;
CREATE INDEX semantic_document_entry_idx ON semantic_document(entry_ref, chunk_index);
CREATE TABLE embedding_run (
    run_id TEXT PRIMARY KEY,
    model_tag TEXT NOT NULL,
    model_digest TEXT NOT NULL,
    dimension INTEGER NOT NULL CHECK(dimension > 0),
    dtype TEXT NOT NULL CHECK(dtype = 'float32-le'),
    prompt_version TEXT NOT NULL,
    instruction TEXT NOT NULL,
    database_revision TEXT NOT NULL CHECK(length(database_revision) = 71),
    status TEXT NOT NULL CHECK(status IN ('in_progress','complete','failed')),
    embedded_documents INTEGER NOT NULL DEFAULT 0 CHECK(embedded_documents >= 0),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at TEXT
) STRICT;
CREATE TABLE candidate_score (
    run_id TEXT NOT NULL REFERENCES embedding_run(run_id) ON DELETE CASCADE,
    left_entry_ref TEXT NOT NULL REFERENCES entry(entry_ref),
    right_entry_ref TEXT NOT NULL REFERENCES entry(entry_ref),
    ascii_lemma TEXT NOT NULL,
    proper INTEGER NOT NULL CHECK(proper IN (0, 1)),
    packet_revision TEXT CHECK(packet_revision IS NULL OR length(packet_revision) = 71),
    blocking_reason TEXT,
    max_cosine REAL NOT NULL,
    top3_mean_cosine REAL NOT NULL,
    best_left_document_ref TEXT NOT NULL REFERENCES semantic_document(document_ref),
    best_right_document_ref TEXT NOT NULL REFERENCES semantic_document(document_ref),
    left_rank INTEGER,
    right_rank INTEGER,
    left_margin REAL,
    right_margin REAL,
    mutual_top1 INTEGER NOT NULL CHECK(mutual_top1 IN (0, 1)),
    signals_json TEXT NOT NULL,
    PRIMARY KEY(run_id, left_entry_ref, right_entry_ref),
    CHECK(left_entry_ref < right_entry_ref)
) STRICT;
CREATE INDEX candidate_score_lookup_idx
    ON candidate_score(run_id, left_entry_ref, right_entry_ref);
CREATE INDEX candidate_score_rank_idx
    ON candidate_score(run_id, mutual_top1, max_cosine DESC);
"""


CACHE_SCHEMA_SQL = """
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS embedding (
    document_ref TEXT NOT NULL,
    input_sha256 TEXT NOT NULL CHECK(length(input_sha256) = 71),
    model_digest TEXT NOT NULL,
    prompt_version TEXT NOT NULL,
    dimension INTEGER NOT NULL CHECK(dimension > 0),
    dtype TEXT NOT NULL CHECK(dtype = 'float32-le'),
    normalized INTEGER NOT NULL CHECK(normalized = 1),
    vector BLOB NOT NULL,
    vector_sha256 TEXT NOT NULL CHECK(length(vector_sha256) = 71),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY(document_ref, input_sha256, model_digest, prompt_version)
) STRICT;
CREATE INDEX IF NOT EXISTS embedding_run_lookup_idx
    ON embedding(model_digest, prompt_version, document_ref);
"""


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def sha256_bytes(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def sha256_text(value: str) -> str:
    return sha256_bytes(value.encode("utf-8"))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def open_text(path: Path, mode: str) -> TextIO:
    if path.suffix == ".gz":
        return gzip.open(path, mode + "t", encoding="utf-8", newline="")
    return path.open(mode, encoding="utf-8", newline="")


def read_packets(path: Path) -> Iterator[dict[str, Any]]:
    with open_text(path, "r") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise LexicalEmbeddingError(f"{path}:{line_number}: invalid JSON") from error
            if not isinstance(record, dict) or record.get("schema") != INPUT_SCHEMA:
                raise LexicalEmbeddingError(
                    f"{path}:{line_number}: expected schema {INPUT_SCHEMA!r}"
                )
            revision = record.get("revision")
            if not isinstance(revision, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", revision):
                raise LexicalEmbeddingError(f"{path}:{line_number}: invalid packet revision")
            yield record


def plain_text(value: Any) -> str:
    if value is None:
        return ""
    text = html.unescape(TAG.sub(" ", str(value)))
    return SPACE.sub(" ", unicodedata.normalize("NFC", text)).strip()


def ascii_word(value: str) -> str:
    decomposed = unicodedata.normalize("NFKD", value.lower())
    return "".join(character for character in decomposed if "a" <= character <= "z")


def semantic_quality(text: str, fallback: bool) -> str:
    if CROSSREF_ONLY.match(text):
        return "crossref_only"
    return "entry_fallback" if fallback else "sense"


def chunk_text(text: str, maximum: int = 12_000, overlap: int = 500) -> list[tuple[int, int, str]]:
    if maximum <= overlap or overlap < 0:
        raise LexicalEmbeddingError("chunk limits must satisfy maximum > overlap >= 0")
    if len(text) <= maximum:
        return [(0, len(text), text)]
    chunks: list[tuple[int, int, str]] = []
    start = 0
    while start < len(text):
        hard_end = min(len(text), start + maximum)
        end = hard_end
        if hard_end < len(text):
            floor = start + maximum // 2
            candidates = [
                text.rfind(marker, floor, hard_end)
                for marker in ("\n\n", ". ", "; ", ": ")
            ]
            boundary = max(candidates)
            if boundary >= floor:
                end = boundary + (2 if text[boundary:boundary + 2] != "\n\n" else 2)
        piece = text[start:end].strip()
        if piece:
            actual_start = start + len(text[start:end]) - len(text[start:end].lstrip())
            chunks.append((actual_start, actual_start + len(piece), piece))
        if end >= len(text):
            break
        start = max(start + 1, end - overlap)
    return chunks


def format_embedding_input(text: str, language: str, instruction: str = DEFAULT_INSTRUCTION) -> str:
    return f"Instruct: {instruction}\nQuery: [language={language}]\n{text}"


def connect_evidence(path: Path, readonly: bool = False) -> sqlite3.Connection:
    if readonly:
        uri = f"file:{path.resolve()}?mode=ro&immutable=1"
        connection = sqlite3.connect(uri, uri=True)
    else:
        connection = sqlite3.connect(path)
        connection.execute("PRAGMA journal_mode = WAL")
        connection.execute("PRAGMA synchronous = NORMAL")
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    return connection


def source_defaults(source: str) -> tuple[str, str, bool]:
    return {
        "ls_dict": ("lewis", "en", True),
        "gaffiot": ("gaffiot", "fr", True),
        "faria_v3": ("faria", "pt", True),
        "latin_german": ("latin-german", "de", False),
        "words": ("current-words", "en", False),
    }.get(source, (source, "und", False))


def external_entry_ref(entry: dict[str, Any]) -> str:
    return f'{entry["source"]}:{entry["source_entry_id"]}'


def words_entry_ref(entry: dict[str, Any]) -> str:
    return f'words:{int(entry["entry_id"])}'


def _insert_source(connection: sqlite3.Connection, source: str, entry: dict[str, Any]) -> None:
    family, language, primary = source_defaults(source)
    connection.execute(
        "INSERT OR IGNORE INTO source_snapshot "
        "(source_key,source_family,language,primary_authority) VALUES (?,?,?,?)",
        (
            source,
            str(entry.get("source_family") or family),
            str(entry.get("language") or language),
            int(bool(entry.get("primary_consensus_authority", primary))),
        ),
    )


def _form_rows(entry_ref: str, entry: dict[str, Any], words: bool) -> list[tuple[Any, ...]]:
    result: list[tuple[str, str | None, str, str, str]] = []
    if words:
        for stem in entry.get("stems", []):
            display = plain_text(stem.get("ascii"))
            if display:
                result.append(("stem", display, display.lower(), f'stem:{stem.get("slot")}', canonical_json(stem.get("quantity_observations", []))))
    else:
        citation = entry.get("citation", {})
        lemma = plain_text(citation.get("source"))
        if lemma:
            result.append(("lemma", lemma, citation.get("ascii"), "citation", canonical_json(citation.get("quantity_observations", []))))
        morphology = entry.get("morphology", {})
        for form in list(morphology.get("basic_forms", [])) + list(morphology.get("forms", [])):
            display = plain_text(form.get("form"))
            if not display:
                continue
            kind = str(form.get("kind") or ("primary" if form.get("is_primary") else "form"))
            role = "variant" if kind == "orth" else "principal_part"
            result.append(
                (
                    role, display,
                    form.get("form_norm") or form.get("form_search") or ascii_word(display),
                    kind, "[]",
                )
            )
        tokens = morphology.get("inflection_type_tokens", [])
        if isinstance(tokens, list):
            for token in tokens:
                display = plain_text(token.get("text") if isinstance(token, dict) else token)
                if display:
                    result.append(("principal_part", display, ascii_word(display), "itype", "[]"))
    unique = []
    seen = set()
    for row in result:
        key = row[:4]
        if key not in seen:
            seen.add(key)
            unique.append(row)
    return [(entry_ref, ordinal, *row) for ordinal, row in enumerate(unique)]


def _sense_candidates(entry: dict[str, Any], words: bool) -> list[tuple[str, str | None, str, str, str]]:
    if words:
        text = plain_text(entry.get("meaning"))
        return [("meaning", None, "en", "", text)] if text else []
    language = str(entry.get("language") or source_defaults(str(entry.get("source")))[1])
    meanings = entry.get("meanings", {})
    result = []
    for ordinal, sense in enumerate(meanings.get("senses", [])):
        text = next(
            (plain_text(sense.get(key)) for key in ("definition_raw", "gloss", "gloss_raw") if plain_text(sense.get(key))),
            "",
        )
        if text:
            identifier = str(sense.get("sense_id", ordinal))
            result.append((identifier, str(sense.get("parent_id")) if sense.get("parent_id") is not None else None, language, plain_text(sense.get("label_raw") or sense.get("n_label")), text))
    if result:
        return result
    fallbacks: list[tuple[str, str]] = []
    source = entry.get("source")
    if source == "ls_dict":
        fallbacks = [("pt", meanings.get("gloss_pt")), ("pt", meanings.get("definition_pt")), ("en", meanings.get("head"))]
    elif source == "gaffiot":
        fallbacks = [("fr", meanings.get("head"))]
    else:
        fallbacks = [(language, meanings.get("definition")), (language, meanings.get("head"))]
    for fallback_language, value in fallbacks:
        text = plain_text(value)
        if text:
            return [("fallback", None, fallback_language, "", text)]
    return []


def _insert_entry(
    connection: sqlite3.Connection,
    packet: dict[str, Any],
    entry: dict[str, Any],
    words: bool,
    counts: Counter[str],
) -> None:
    source = "words" if words else str(entry["source"])
    _insert_source(connection, source, entry)
    entry_ref = words_entry_ref(entry) if words else external_entry_ref(entry)
    key = packet["key"]
    if words:
        lemma_display = str(key["ascii_lemma"])
        lexical = {}
        part = entry.get("part_of_speech")
        gender = None
        attribute = entry.get("class_attribute") or {}
        if part == "NOUN":
            gender = {"masculine": "m", "feminine": "f", "neuter": "n", "common": "c"}.get(attribute.get("name"))
        meanings = {"meaning": entry.get("meaning")}
        metadata = entry.get("metadata_and_flags", {})
        morphology = {"stems": entry.get("stems", []), "paradigm": entry.get("paradigm")}
        source_entry_id = str(entry["entry_id"])
        matches = [int(entry["entry_id"])]
        homograph = None
        indeclinable = None
        quantity = next((stem.get("quantity_observations", []) for stem in entry.get("stems", []) if stem.get("slot") == 1), [])
    else:
        citation = entry.get("citation", {})
        lexical = entry.get("lexical", {})
        lemma_display = str(citation.get("source") or key["ascii_lemma"])
        part = lexical.get("part_of_speech")
        gender = lexical.get("gender")
        meanings = entry.get("meanings", {})
        metadata = entry.get("metadata_and_flags", {})
        morphology = entry.get("morphology", {})
        source_entry_id = str(entry["source_entry_id"])
        matches = entry.get("current_words_match_ids", [])
        homograph = entry.get("homograph_number")
        indeclinable = lexical.get("indeclinable")
        quantity = citation.get("quantity_observations", [])
    if words:
        first_stem = next(
            (plain_text(stem.get("ascii")) for stem in entry.get("stems", []) if plain_text(stem.get("ascii"))),
            str(key["ascii_lemma"]),
        )
        lemma_display = first_stem
        entry_ascii_lemma = ascii_word(first_stem) or str(key["ascii_lemma"])
    else:
        entry_ascii_lemma = str(key["ascii_lemma"])
    candidates = _sense_candidates(entry, words)
    semantic_status = "available" if candidates else "missing"
    content = {
        "lemma": lemma_display,
        "lexical": lexical,
        "morphology": morphology,
        "meanings": meanings,
        "metadata": metadata,
    }
    content_digest = sha256_text(canonical_json(content))
    cursor = connection.execute(
        "INSERT OR IGNORE INTO entry VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (
            entry_ref, source, source_defaults(source)[0] if words else entry.get("source_family"),
            source_entry_id, entry_ascii_lemma, int(bool(key["proper"])), lemma_display,
            homograph, lexical.get("part_of_speech_raw") if lexical else None, part,
            lexical.get("gender_raw") if lexical else None, gender,
            None if indeclinable is None else int(bool(indeclinable)), canonical_json(matches),
            canonical_json(quantity), canonical_json(morphology), canonical_json(meanings),
            canonical_json(metadata), semantic_status, packet["revision"], content_digest,
        ),
    )
    connection.execute(
        "INSERT OR IGNORE INTO entry_bucket VALUES (?,?,?,?)",
        (entry_ref, key["ascii_lemma"], int(bool(key["proper"])), packet["revision"]),
    )
    counts["entry_bucket_links"] += 1
    if cursor.rowcount == 0:
        existing = connection.execute(
            "SELECT content_sha256 FROM entry WHERE entry_ref=?", (entry_ref,)
        ).fetchone()
        if existing is None or existing["content_sha256"] != content_digest:
            raise LexicalEmbeddingError(
                f"entry {entry_ref!r} changed across comparison packets"
            )
        counts["repeated_entry_links"] += 1
        return
    forms = _form_rows(entry_ref, entry, words)
    connection.executemany(
        "INSERT INTO entry_form (entry_ref,ordinal,role,form_display,form_ascii,source_kind,quantity_json) VALUES (?,?,?,?,?,?,?)",
        forms,
    )
    counts["forms"] += len(forms)
    if not words:
        aliases = metadata.get("cross_references", [])
        for ordinal, alias in enumerate(aliases):
            target = plain_text(alias.get("target_text") or alias.get("target_raw"))
            if not target:
                continue
            relation = str(alias.get("rel_type") or alias.get("relation_kind") or "reference").lower()
            pure = bool(alias.get("is_pure_alias")) or relation in PURE_ALIAS_RELATIONS
            target_ascii = plain_text(alias.get("target_norm") or alias.get("target_search")) or ascii_word(target)
            connection.execute(
                "INSERT INTO entry_alias VALUES (?,?,?,?,?,?)",
                (entry_ref, ordinal, target, target_ascii or None, relation, int(pure)),
            )
            counts["aliases"] += 1
    for ordinal, (source_sense_id, parent_id, language, label, text) in enumerate(candidates):
        fallback = source_sense_id in {"fallback", "meaning"}
        sense_ref = f"{entry_ref}:sense:{source_sense_id}"
        connection.execute(
            "INSERT INTO sense VALUES (?,?,?,?,?,?,?,?,?)",
            (
                sense_ref, entry_ref, source_sense_id, parent_id, ordinal, language,
                label or None, text, sha256_text(text),
            ),
        )
        counts["senses"] += 1
        quality = semantic_quality(text, fallback)
        for chunk_index, (start, end, piece) in enumerate(chunk_text(text)):
            document_ref = f"{sense_ref}:chunk:{chunk_index}"
            connection.execute(
                "INSERT INTO semantic_document VALUES (?,?,?,?,?,?,?,?,?,?)",
                (
                    document_ref, entry_ref, sense_ref, language, quality, chunk_index,
                    start, end, piece, sha256_text(piece),
                ),
            )
            counts["documents"] += 1
            counts[f"documents:{quality}"] += 1
    counts["entries"] += 1
    counts[f"entries:{semantic_status}"] += 1


def _load_report(path: Path | None) -> tuple[dict[str, Any], str | None]:
    if path is None:
        return {}, None
    raw = path.read_bytes()
    try:
        value = json.loads(raw)
    except json.JSONDecodeError as error:
        raise LexicalEmbeddingError(f"{path}: invalid report JSON") from error
    if not isinstance(value, dict):
        raise LexicalEmbeddingError(f"{path}: report must be an object")
    return value, sha256_bytes(raw)


def build_database(input_path: Path, output_path: Path, report_path: Path | None = None) -> dict[str, Any]:
    if output_path.exists():
        raise LexicalEmbeddingError(f"refusing to overwrite existing database: {output_path}")
    report, report_digest = _load_report(report_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(prefix=output_path.name + ".", suffix=".tmp", dir=output_path.parent)
    os.close(handle)
    temporary = Path(temporary_name)
    counts: Counter[str] = Counter()
    try:
        connection = sqlite3.connect(temporary)
        connection.row_factory = sqlite3.Row
        connection.executescript(SCHEMA_SQL)
        input_digest = sha256_file(input_path)
        connection.execute(
            "INSERT INTO input_snapshot VALUES (?,?,?,?)",
            ("comparison_dump", str(input_path), input_path.stat().st_size, input_digest),
        )
        if report_path is not None and report_digest is not None:
            connection.execute(
                "INSERT INTO input_snapshot VALUES (?,?,?,?)",
                ("comparison_report", str(report_path), report_path.stat().st_size, report_digest),
            )
        for item in report.get("source_manifest", []):
            source = str(item["source_key"])
            family, language, primary = source_defaults(source)
            connection.execute(
                "INSERT OR REPLACE INTO source_snapshot VALUES (?,?,?,?,?,?,?,?)",
                (
                    source, item.get("source_family", family), item.get("language", language),
                    int(item.get("primary_authority", primary)), item.get("path"),
                    item.get("size_bytes"), item.get("sha256"), item.get("schema_signature"),
                ),
            )
        for packet in read_packets(input_path):
            counts["packets"] += 1
            for entry in packet.get("source_entries", []):
                _insert_entry(connection, packet, entry, False, counts)
            for entry in packet.get("current_words_entries", []):
                _insert_entry(connection, packet, entry, True, counts)
        revision_payload = {
            "schema": DATABASE_SCHEMA,
            "input_sha256": input_digest,
            "report_sha256": report_digest,
            "counts": dict(sorted(counts.items())),
        }
        database_revision = sha256_text(canonical_json(revision_payload))
        metadata = {
            "schema": DATABASE_SCHEMA,
            "database_revision": database_revision,
            "input_sha256": input_digest,
            "report_sha256": report_digest or "",
        }
        connection.executemany("INSERT INTO metadata VALUES (?,?)", sorted(metadata.items()))
        connection.commit()
        violations = list(connection.execute("PRAGMA foreign_key_check"))
        if violations:
            raise LexicalEmbeddingError(f"foreign key violations: {violations[:3]}")
        connection.execute("PRAGMA journal_mode = DELETE")
        connection.execute("VACUUM")
        connection.close()
        os.replace(temporary, output_path)
    except Exception:
        try:
            connection.close()
        except Exception:
            pass
        temporary.unlink(missing_ok=True)
        raise
    return {
        "schema": "whitakers-words.lexical-embedding-build-report.v1",
        "output": str(output_path),
        "database_revision": database_revision,
        "counts": dict(sorted(counts.items())),
        "read_policy": "comparison dump only; external source databases were not opened",
    }


def database_metadata(connection: sqlite3.Connection) -> dict[str, str]:
    return {str(row["key"]): str(row["value"]) for row in connection.execute("SELECT key,value FROM metadata")}


def ensure_cache(connection: sqlite3.Connection) -> None:
    connection.executescript(CACHE_SCHEMA_SQL)
    connection.commit()


def pair_key(left: str, right: str) -> tuple[str, str]:
    return (left, right) if left < right else (right, left)
