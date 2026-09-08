#!/usr/bin/env python3

"""Generate resumable Qwen embeddings for lexical semantic documents."""

from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np
import requests

import lexical_embedding_db as lexical


@dataclass(frozen=True)
class PendingDocument:
    document_ref: str
    input_text: str
    input_sha256: str


class OllamaClient:
    def __init__(self, base_url: str, timeout: int = 300) -> None:
        value = base_url.rstrip("/")
        if value.endswith("/api/embed"):
            value = value[: -len("/api/embed")]
        self.base_url = value
        self.timeout = timeout

    def model_digest(self, model: str) -> str:
        response = requests.get(f"{self.base_url}/api/tags", timeout=self.timeout)
        response.raise_for_status()
        models = response.json().get("models", [])
        for item in models:
            if item.get("name") == model or item.get("model") == model:
                digest = item.get("digest")
                if isinstance(digest, str) and digest:
                    return digest
        available = sorted(
            str(item.get("name") or item.get("model")) for item in models
        )
        raise lexical.LexicalEmbeddingError(
            f"Ollama model {model!r} is not installed; available: {', '.join(available)}"
        )

    def embed(self, texts: list[str], model: str) -> list[list[float]]:
        response = requests.post(
            f"{self.base_url}/api/embed",
            json={"model": model, "input": texts, "truncate": False},
            timeout=self.timeout,
        )
        response.raise_for_status()
        embeddings = response.json().get("embeddings")
        if not isinstance(embeddings, list) or len(embeddings) != len(texts):
            raise lexical.LexicalEmbeddingError(
                f"Ollama returned {len(embeddings) if isinstance(embeddings, list) else 'no'} "
                f"embeddings for {len(texts)} inputs"
            )
        return embeddings


def normalized_blob(vector: Iterable[float], expected_dimension: int) -> tuple[bytes, str]:
    values = np.asarray(list(vector), dtype="<f4")
    if values.ndim != 1 or len(values) != expected_dimension:
        raise lexical.LexicalEmbeddingError(
            f"expected embedding dimension {expected_dimension}, got {values.shape}"
        )
    if not np.all(np.isfinite(values)):
        raise lexical.LexicalEmbeddingError("embedding contains a non-finite value")
    norm = float(np.linalg.norm(values))
    if norm <= 0.0:
        raise lexical.LexicalEmbeddingError("embedding has zero norm")
    values = np.asarray(values / norm, dtype="<f4")
    blob = values.tobytes(order="C")
    return blob, lexical.sha256_bytes(blob)


def run_identifier(
    database_revision: str,
    model: str,
    model_digest: str,
    dimension: int,
    instruction: str,
) -> str:
    payload = lexical.canonical_json(
        {
            "database_revision": database_revision,
            "model": model,
            "model_digest": model_digest,
            "dimension": dimension,
            "dtype": "float32-le",
            "prompt_version": lexical.PROMPT_VERSION,
            "instruction": instruction,
        }
    )
    return "embedding:" + hashlib.sha256(payload.encode("utf-8")).hexdigest()


def load_documents(evidence_path: Path, instruction: str) -> tuple[str, list[PendingDocument]]:
    with lexical.connect_evidence(evidence_path, readonly=True) as connection:
        metadata = lexical.database_metadata(connection)
        if metadata.get("schema") != lexical.DATABASE_SCHEMA:
            raise lexical.LexicalEmbeddingError(
                f"{evidence_path}: expected schema {lexical.DATABASE_SCHEMA!r}"
            )
        revision = metadata["database_revision"]
        documents = []
        for row in connection.execute(
            "SELECT document_ref,language,semantic_text FROM semantic_document ORDER BY document_ref"
        ):
            formatted = lexical.format_embedding_input(
                str(row["semantic_text"]), str(row["language"]), instruction
            )
            documents.append(
                PendingDocument(
                    str(row["document_ref"]), formatted, lexical.sha256_text(formatted)
                )
            )
    return revision, documents


def cache_connection(path: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(path)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA journal_mode = WAL")
    connection.execute("PRAGMA synchronous = NORMAL")
    lexical.ensure_cache(connection)
    return connection


def missing_documents(
    connection: sqlite3.Connection,
    documents: list[PendingDocument],
    model_digest: str,
) -> list[PendingDocument]:
    existing = {
        (str(row["document_ref"]), str(row["input_sha256"]))
        for row in connection.execute(
            "SELECT document_ref,input_sha256 FROM embedding "
            "WHERE model_digest=? AND prompt_version=?",
            (model_digest, lexical.PROMPT_VERSION),
        )
    }
    return [
        document
        for document in documents
        if (document.document_ref, document.input_sha256) not in existing
    ]


def chunks(values: list[PendingDocument], size: int) -> Iterable[list[PendingDocument]]:
    for offset in range(0, len(values), size):
        yield values[offset:offset + size]


def generate(arguments: argparse.Namespace, client: OllamaClient | None = None) -> dict[str, Any]:
    if arguments.batch_size <= 0:
        raise lexical.LexicalEmbeddingError("--batch-size must be positive")
    if arguments.expected_dimension <= 0:
        raise lexical.LexicalEmbeddingError("--expected-dimension must be positive")
    client = client or OllamaClient(arguments.ollama_url, arguments.timeout)
    database_revision, documents = load_documents(arguments.evidence, arguments.instruction)

    # Preflight is deliberately completed before either generated database is opened for writing.
    model_digest = client.model_digest(arguments.model)
    probe = client.embed([documents[0].input_text], arguments.model) if documents else []
    if probe:
        normalized_blob(probe[0], arguments.expected_dimension)
    run_id = run_identifier(
        database_revision, arguments.model, model_digest,
        arguments.expected_dimension, arguments.instruction,
    )

    cache = cache_connection(arguments.cache)
    pending = missing_documents(cache, documents, model_digest)
    evidence = lexical.connect_evidence(arguments.evidence)
    evidence.execute(
        "INSERT INTO embedding_run "
        "(run_id,model_tag,model_digest,dimension,dtype,prompt_version,instruction,database_revision,status,embedded_documents) "
        "VALUES (?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(run_id) DO UPDATE SET status='in_progress', completed_at=NULL",
        (
            run_id, arguments.model, model_digest, arguments.expected_dimension,
            "float32-le", lexical.PROMPT_VERSION, arguments.instruction,
            database_revision, "in_progress", len(documents) - len(pending),
        ),
    )
    evidence.commit()
    embedded = len(documents) - len(pending)
    batches = 0
    try:
        for batch in chunks(pending, arguments.batch_size):
            vectors = client.embed([item.input_text for item in batch], arguments.model)
            if len(vectors) != len(batch):
                raise lexical.LexicalEmbeddingError("embedding batch cardinality mismatch")
            rows = []
            for document, vector in zip(batch, vectors):
                blob, vector_hash = normalized_blob(vector, arguments.expected_dimension)
                rows.append(
                    (
                        document.document_ref, document.input_sha256, model_digest,
                        lexical.PROMPT_VERSION, arguments.expected_dimension,
                        "float32-le", 1, blob, vector_hash,
                    )
                )
            with cache:
                cache.executemany(
                    "INSERT OR REPLACE INTO embedding "
                    "(document_ref,input_sha256,model_digest,prompt_version,dimension,dtype,normalized,vector,vector_sha256) "
                    "VALUES (?,?,?,?,?,?,?,?,?)",
                    rows,
                )
            embedded += len(batch)
            batches += 1
            evidence.execute(
                "UPDATE embedding_run SET embedded_documents=? WHERE run_id=?",
                (embedded, run_id),
            )
            evidence.commit()
            print(f"batch {batches}: {embedded}/{len(documents)} documents")
    except Exception:
        evidence.execute("UPDATE embedding_run SET status='failed' WHERE run_id=?", (run_id,))
        evidence.commit()
        raise
    else:
        evidence.execute(
            "UPDATE embedding_run SET status='complete',embedded_documents=?,completed_at=CURRENT_TIMESTAMP WHERE run_id=?",
            (embedded, run_id),
        )
        evidence.commit()
    finally:
        evidence.close()
        cache.close()
    return {
        "schema": "whitakers-words.lexical-embedding-generation-report.v1",
        "run_id": run_id,
        "model": arguments.model,
        "model_digest": model_digest,
        "dimension": arguments.expected_dimension,
        "documents": len(documents),
        "new_documents": len(pending),
        "batches": batches,
        "cache": str(arguments.cache),
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--model", default=lexical.DEFAULT_MODEL)
    parser.add_argument("--ollama-url", default=lexical.DEFAULT_OLLAMA_URL)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--expected-dimension", type=int, default=4096)
    parser.add_argument("--instruction", default=lexical.DEFAULT_INSTRUCTION)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    report = generate(arguments)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"embedding run {report['run_id']} is complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
