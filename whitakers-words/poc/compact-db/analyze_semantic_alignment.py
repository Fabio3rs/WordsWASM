#!/usr/bin/env python3

"""Rank semantic alignments without automatically merging dictionary entries."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import html
import itertools
import json
import math
import re
import sqlite3
import unicodedata
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Iterator, TextIO


INPUT_SCHEMA = "whitakers-words.lexical-comparison-packet.v1"
OUTPUT_SCHEMA = "whitakers-words.semantic-alignment-review.v1"
REPORT_SCHEMA = "whitakers-words.semantic-alignment-report.v1"
ALGORITHM = "explainable-multilingual-section-overlap-v1"
OUTPUT_SCHEMA_V2 = "whitakers-words.semantic-alignment-review.v2"
REPORT_SCHEMA_V2 = "whitakers-words.semantic-alignment-report.v2"
ALGORITHM_V2 = "explainable-multilingual-section-overlap-qwen3-v2"
PRIMARY_FAMILIES = frozenset({"lewis", "gaffiot", "faria"})

TOKEN = re.compile(r"[a-z]{3,}")
TAG = re.compile(r"<[^>]+>")
SPACE = re.compile(r"\s+")
COMMON_STOPWORDS = frozenset(
    {
        "adj", "adv", "alii", "also", "apud", "circa", "definition",
        "aug", "bell", "caes", "cels", "cic", "col", "curt", "dig",
        "dicionario", "enn", "entry", "ep", "etc", "exemplo", "fam",
        "fast", "fig", "figurado", "fin", "gell", "gloss", "hist", "hor",
        "idem", "inst", "isto", "just", "leg", "lemma", "literal", "liv",
        "lucr", "mart", "met", "morphology", "nat", "nep", "ov", "plaut",
        "plin", "poet", "prop", "proprio", "quint", "sall", "sen", "sent",
        "sentido", "serv", "sil", "stat", "subst", "suet", "tac", "ter",
        "term", "tib", "varr", "veg", "verb", "verg", "vide", "vulg",
    }
)
STOPWORDS = {
    "pt": frozenset(
        {
            "aos", "aquela", "aquele", "com", "como", "das", "depois",
            "dos", "ela", "ele", "em", "entre", "essa", "esse", "esta",
            "este", "isto", "mas", "nas", "nos", "para", "pela", "pelo",
            "por", "que", "sem", "ser", "sob", "sua", "seu", "uma", "uns",
        }
    ),
    "en": frozenset(
        {
            "also", "and", "are", "but", "for", "from", "has", "have",
            "into", "its", "not", "one", "that", "the", "their", "this",
            "through", "upon", "was", "were", "with",
        }
    ),
    "fr": frozenset(
        {
            "aux", "avec", "comme", "dans", "des", "est", "les", "par",
            "pas", "pour", "que", "qui", "sans", "sur", "une",
        }
    ),
    "de": frozenset(
        {
            "als", "auf", "aus", "bei", "das", "dem", "den", "der", "des",
            "die", "ein", "eine", "einer", "eines", "fur", "ist", "mit",
            "oder", "und", "von", "zu", "zum", "zur",
        }
    ),
}
SUFFIXES = {
    "pt": ("amentos", "imento", "mente", "acoes", "acao", "icoes", "ico", "ica", "icos", "icas", "osos", "osas", "oso", "osa", "es", "s"),
    "en": ("ingly", "edly", "ation", "ments", "ment", "ness", "ing", "ed", "es", "s"),
    "fr": ("ements", "ement", "ations", "ation", "iques", "ique", "euses", "eux", "es", "s"),
    "de": ("ungen", "ung", "ischen", "isch", "ern", "en", "er", "es", "e", "n", "s"),
}


class AlignmentError(ValueError):
    """The comparison dump cannot safely be analyzed."""


class EmbeddingEvidence:
    def __init__(
        self, path: Path, requested_run_id: str | None,
        calibration_path: Path | None,
    ) -> None:
        uri = f"file:{path.resolve()}?mode=ro&immutable=1"
        connection = sqlite3.connect(uri, uri=True)
        connection.row_factory = sqlite3.Row
        try:
            metadata = dict(connection.execute("SELECT key,value FROM metadata"))
            if metadata.get("schema") != "whitakers-words.lexical-embedding-evidence.v1":
                raise AlignmentError(f"{path}: not a lexical embedding evidence database")
            if requested_run_id:
                run = connection.execute(
                    "SELECT * FROM embedding_run WHERE run_id=?", (requested_run_id,)
                ).fetchone()
            else:
                run = connection.execute(
                    "SELECT * FROM embedding_run WHERE status='complete' "
                    "ORDER BY completed_at DESC,run_id DESC LIMIT 1"
                ).fetchone()
            if run is None or run["status"] != "complete":
                raise AlignmentError(f"{path}: no complete matching embedding run")
            self.run_id = str(run["run_id"])
            self.model = str(run["model_tag"])
            self.model_digest = str(run["model_digest"])
            self.calibration: dict[str, Any] | None = None
            if calibration_path is not None:
                try:
                    calibration = json.loads(calibration_path.read_text(encoding="utf-8"))
                except json.JSONDecodeError as error:
                    raise AlignmentError(f"{calibration_path}: invalid calibration JSON") from error
                required = {
                    "schema", "run_id", "minimum_precision", "cosine_threshold",
                    "minimum_margin", "promotion_allowed", "gold_sha256", "policy",
                }
                if set(calibration) != required:
                    raise AlignmentError(f"{calibration_path}: invalid calibration fields")
                if calibration.get("schema") != "whitakers-words.lexical-embedding-calibration.v1":
                    raise AlignmentError(f"{calibration_path}: invalid calibration schema")
                if calibration.get("run_id") != self.run_id:
                    raise AlignmentError(f"{calibration_path}: calibration belongs to another embedding run")
                if calibration.get("policy") != "mutual_top1_and_no_structural_blocker":
                    raise AlignmentError(f"{calibration_path}: unsupported calibration policy")
                if not isinstance(calibration.get("gold_sha256"), str) or not re.fullmatch(
                    r"sha256:[0-9a-f]{64}", calibration["gold_sha256"]
                ):
                    raise AlignmentError(f"{calibration_path}: invalid gold SHA-256")
                for key in ("minimum_precision", "cosine_threshold", "minimum_margin"):
                    value = calibration.get(key)
                    if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0.0 <= float(value) <= 1.0:
                        raise AlignmentError(f"{calibration_path}: invalid {key}")
                if float(calibration["minimum_precision"]) <= 0.0:
                    raise AlignmentError(f"{calibration_path}: minimum_precision must be positive")
                if calibration.get("promotion_allowed") is not True:
                    raise AlignmentError(f"{calibration_path}: calibration did not pass its precision gate")
                self.calibration = calibration
            self.rows = {
                tuple(sorted((str(row["left_entry_ref"]), str(row["right_entry_ref"])))): dict(row)
                for row in connection.execute(
                    "SELECT * FROM candidate_score WHERE run_id=?", (self.run_id,)
                )
            }
        finally:
            connection.close()

    def comparison(
        self, left: str, right: str, expected_packet_revision: str | None = None,
    ) -> dict[str, Any]:
        row = self.rows.get(tuple(sorted((left, right))))
        if row is None:
            return {
                "status": "not_scored",
                "run_id": self.run_id,
                "model": self.model,
                "model_digest": self.model_digest,
                "calibrated_support": False,
            }
        if expected_packet_revision is not None and row.get("packet_revision") != expected_packet_revision:
            return {
                "status": "stale_packet_revision",
                "run_id": self.run_id,
                "model": self.model,
                "model_digest": self.model_digest,
                "calibrated_support": False,
            }
        minimum_margin = None
        if row["left_margin"] is not None and row["right_margin"] is not None:
            minimum_margin = min(float(row["left_margin"]), float(row["right_margin"]))
        support = False
        if self.calibration is not None:
            support = bool(
                row["blocking_reason"] is None
                and row["mutual_top1"]
                and minimum_margin is not None
                and float(row["max_cosine"]) >= float(self.calibration["cosine_threshold"])
                and minimum_margin >= float(self.calibration["minimum_margin"])
            )
        return {
            "status": "scored",
            "run_id": self.run_id,
            "model": self.model,
            "model_digest": self.model_digest,
            "max_cosine": float(row["max_cosine"]),
            "top3_mean_cosine": float(row["top3_mean_cosine"]),
            "best_left_document_ref": row["best_left_document_ref"],
            "best_right_document_ref": row["best_right_document_ref"],
            "left_rank": row["left_rank"],
            "right_rank": row["right_rank"],
            "left_margin": row["left_margin"],
            "right_margin": row["right_margin"],
            "mutual_top1": bool(row["mutual_top1"]),
            "blocking_reason": row["blocking_reason"],
            "calibrated_support": support,
        }


@dataclass(frozen=True)
class Section:
    node_ref: str
    section_ref: str
    language: str
    text: str
    tokens: frozenset[str]


@dataclass(frozen=True)
class Node:
    ref: str
    source: str
    family: str
    primary: bool
    part: str | None
    gender: str | None
    indeclinable: bool | None
    homograph_number: int | None
    current_match_ids: tuple[int, ...]
    quantity: tuple[tuple[int, str], ...]
    label: str
    sections: tuple[Section, ...]


class UnionFind:
    def __init__(self, nodes: Iterable[str], families: dict[str, str]) -> None:
        self.parent = {node: node for node in nodes}
        self.families = {node: {families[node]} for node in nodes}

    def find(self, node: str) -> str:
        parent = self.parent[node]
        if parent != node:
            self.parent[node] = self.find(parent)
        return self.parent[node]

    def union_without_family_collision(self, left: str, right: str) -> bool:
        first, second = self.find(left), self.find(right)
        if first == second:
            return True
        if self.families[first] & self.families[second]:
            return False
        if len(self.families[first]) < len(self.families[second]):
            first, second = second, first
        self.parent[second] = first
        self.families[first].update(self.families.pop(second))
        return True


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
                raise AlignmentError(f"{path}:{line_number}: invalid JSON") from error
            if not isinstance(record, dict) or record.get("schema") != INPUT_SCHEMA:
                raise AlignmentError(
                    f"{path}:{line_number}: expected schema {INPUT_SCHEMA!r}"
                )
            yield record


def plain_text(value: Any) -> str:
    if value is None:
        return ""
    text = html.unescape(TAG.sub(" ", str(value)))
    return SPACE.sub(" ", text).strip()


def normalize_token(token: str, language: str) -> str:
    for suffix in SUFFIXES.get(language, ()):
        if token.endswith(suffix) and len(token) - len(suffix) >= 4:
            return token[: -len(suffix)]
    return token


def semantic_tokens(text: str, language: str, lemma: str) -> frozenset[str]:
    ascii_text = "".join(
        character
        for character in unicodedata.normalize("NFKD", plain_text(text).lower())
        if unicodedata.combining(character) == 0
    )
    stop = COMMON_STOPWORDS | STOPWORDS.get(language, frozenset()) | {lemma}
    return frozenset(
        normalized
        for token in TOKEN.findall(ascii_text)
        if token not in stop
        and len(normalized := normalize_token(token, language)) >= 3
        and normalized not in stop
    )


def add_section(
    output: list[Section], seen: set[tuple[str, str]], node_ref: str,
    section_ref: str, language: str, value: Any, lemma: str,
) -> None:
    text = plain_text(value)
    if not text:
        return
    duplicate_key = (language, text)
    if duplicate_key in seen:
        return
    seen.add(duplicate_key)
    tokens = semantic_tokens(text, language, lemma)
    if tokens:
        output.append(Section(node_ref, section_ref, language, text, tokens))


def external_sections(entry: dict[str, Any], lemma: str) -> tuple[Section, ...]:
    source = entry["source"]
    node_ref = f'{source}:{entry["source_entry_id"]}'
    meanings = entry.get("meanings", {})
    metadata = entry.get("metadata_and_flags", {})
    result: list[Section] = []
    seen: set[tuple[str, str]] = set()
    if source == "ls_dict":
        add_section(result, seen, node_ref, "gloss-pt", "pt", meanings.get("gloss_pt"), lemma)
        add_section(result, seen, node_ref, "definition-pt", "pt", meanings.get("definition_pt"), lemma)
        add_section(result, seen, node_ref, "head-en", "en", meanings.get("head"), lemma)
        for sense in meanings.get("senses", []):
            identifier = sense.get("sense_id", "unknown")
            add_section(result, seen, node_ref, f"sense-en:{identifier}", "en", sense.get("gloss") or sense.get("gloss_raw"), lemma)
    elif source == "gaffiot":
        add_section(result, seen, node_ref, "head-fr", "fr", meanings.get("head"), lemma)
        for sense in meanings.get("senses", []):
            identifier = sense.get("sense_id", "unknown")
            add_section(result, seen, node_ref, f"sense-fr:{identifier}", "fr", sense.get("gloss") or sense.get("gloss_raw"), lemma)
    elif source == "faria_v3":
        add_section(result, seen, node_ref, "definition-pt", "pt", meanings.get("definition"), lemma)
        for sense in meanings.get("senses", []):
            identifier = sense.get("sense_id", "unknown")
            add_section(result, seen, node_ref, f"sense-pt:{identifier}", "pt", sense.get("definition_raw"), lemma)
        for index, note in enumerate(metadata.get("notes", []), 1):
            add_section(result, seen, node_ref, f"note-pt:{index}", "pt", note.get("text"), lemma)
    elif source == "latin_german":
        add_section(result, seen, node_ref, "definition-de", "de", meanings.get("definition"), lemma)
    return tuple(result)


def words_sections(entry: dict[str, Any], lemma: str) -> tuple[Section, ...]:
    node_ref = f'words:{entry["entry_id"]}'
    result: list[Section] = []
    add_section(result, set(), node_ref, "meaning-en", "en", entry.get("meaning"), lemma)
    return tuple(result)


def preview_label(entry: dict[str, Any]) -> str:
    meanings = entry.get("meanings", {})
    for key in ("gloss_pt", "definition", "head", "definition_pt"):
        value = plain_text(meanings.get(key))
        if value:
            return value[:240]
    return str(entry.get("citation", {}).get("source", ""))[:240]


def external_node(entry: dict[str, Any], lemma: str) -> Node:
    lexical = entry.get("lexical", {})
    observations = entry.get("citation", {}).get("quantity_observations", [])
    return Node(
        ref=f'{entry["source"]}:{entry["source_entry_id"]}',
        source=entry["source"],
        family=entry["source_family"],
        primary=bool(entry.get("primary_consensus_authority")),
        part=lexical.get("part_of_speech"),
        gender=lexical.get("gender"),
        indeclinable=lexical.get("indeclinable"),
        homograph_number=entry.get("homograph_number"),
        current_match_ids=tuple(entry.get("current_words_match_ids", [])),
        quantity=tuple(
            sorted((int(item["position"]), str(item["quantity"])) for item in observations)
        ),
        label=preview_label(entry),
        sections=external_sections(entry, lemma),
    )


def words_node(entry: dict[str, Any], lemma: str) -> Node:
    observations = next(
        (
            stem.get("quantity_observations", [])
            for stem in entry.get("stems", [])
            if stem.get("slot") == 1
        ),
        [],
    )
    attribute = entry.get("class_attribute") or {}
    gender = None
    if entry.get("part_of_speech") == "NOUN":
        gender = {"masculine": "m", "feminine": "f", "neuter": "n", "common": "c"}.get(attribute.get("name"))
    return Node(
        ref=f'words:{entry["entry_id"]}', source="words", family="current-words",
        primary=False, part=entry.get("part_of_speech"), gender=gender,
        indeclinable=None, homograph_number=None,
        current_match_ids=(int(entry["entry_id"]),),
        quantity=tuple(sorted((int(item["position"]), str(item["quantity"])) for item in observations)),
        label=str(entry.get("meaning", ""))[:240], sections=words_sections(entry, lemma),
    )


def packet_nodes(packet: dict[str, Any]) -> tuple[Node, ...]:
    lemma = packet["key"]["ascii_lemma"]
    external = [external_node(entry, lemma) for entry in packet.get("source_entries", [])]
    words = [words_node(entry, lemma) for entry in packet.get("current_words_entries", [])]
    return tuple(external + words)


def collect_document_frequency(path: Path) -> tuple[Counter[tuple[str, str]], Counter[str]]:
    frequency: Counter[tuple[str, str]] = Counter()
    document_counts: Counter[str] = Counter()
    for packet in read_packets(path):
        for node in packet_nodes(packet):
            for section in node.sections:
                document_counts[section.language] += 1
                frequency.update((section.language, token) for token in section.tokens)
    return frequency, document_counts


def idf(language: str, token: str, frequency: Counter[tuple[str, str]], documents: Counter[str]) -> float:
    return math.log((documents[language] + 1.0) / (frequency[(language, token)] + 1.0)) + 1.0


def section_similarity(
    left: Section, right: Section, frequency: Counter[tuple[str, str]],
    documents: Counter[str],
) -> tuple[float, tuple[str, ...]]:
    if left.language != right.language:
        return 0.0, ()
    shared = left.tokens & right.tokens
    if not shared:
        return 0.0, ()
    language = left.language
    shared_weight = sum(idf(language, token, frequency, documents) for token in shared)
    left_weight = sum(idf(language, token, frequency, documents) for token in left.tokens)
    right_weight = sum(idf(language, token, frequency, documents) for token in right.tokens)
    union_weight = left_weight + right_weight - shared_weight
    containment = shared_weight / min(left_weight, right_weight)
    jaccard = shared_weight / union_weight
    score = 0.75 * containment + 0.25 * jaccard
    ranked = tuple(
        sorted(shared, key=lambda token: (-idf(language, token, frequency, documents), token))[:12]
    )
    return score, ranked


def semantic_comparison(
    left: Node, right: Node, frequency: Counter[tuple[str, str]],
    documents: Counter[str],
) -> dict[str, Any]:
    matches = []
    shared_languages = sorted(
        {section.language for section in left.sections}
        & {section.language for section in right.sections}
    )
    for first in left.sections:
        for second in right.sections:
            score, shared = section_similarity(first, second, frequency, documents)
            if score <= 0:
                continue
            matches.append(
                {
                    "language": first.language,
                    "left_section": first.section_ref,
                    "right_section": second.section_ref,
                    "score": round(score, 6),
                    "shared_terms": list(shared),
                }
            )
    matches.sort(key=lambda item: (-item["score"], item["language"], item["left_section"], item["right_section"]))
    return {
        "status": "comparable" if shared_languages else "no_shared_language",
        "shared_languages": shared_languages,
        "best_score": matches[0]["score"] if matches else 0.0,
        "best_section_matches": matches[:5],
    }


def quantity_comparison(left: Node, right: Node) -> dict[str, Any]:
    first, second = dict(left.quantity), dict(right.quantity)
    positions = sorted(set(first) & set(second))
    agreement = [position for position in positions if first[position] == second[position]]
    conflict = [position for position in positions if first[position] != second[position]]
    return {
        "agreement_positions": agreement,
        "conflict_positions": conflict,
        "comparable_positions": positions,
    }


def pair_record(
    left: Node, right: Node, frequency: Counter[tuple[str, str]],
    documents: Counter[str], strong_score: float, candidate_score: float,
    embedding_evidence: EmbeddingEvidence | None = None,
    packet_revision: str | None = None,
) -> dict[str, Any]:
    semantic = semantic_comparison(left, right, frequency, documents)
    quantities = quantity_comparison(left, right)
    same_family = left.family == right.family
    gender_conflict = bool(left.gender and right.gender and left.gender != right.gender)
    current_overlap = sorted(set(left.current_match_ids) & set(right.current_match_ids))
    score = float(semantic["best_score"])
    if same_family:
        relation = "within_family_contrast"
    elif gender_conflict:
        relation = "structural_conflict"
    elif score >= strong_score and semantic["best_section_matches"]:
        relation = "strong_same_lexeme_candidate"
    elif score >= candidate_score and semantic["best_section_matches"]:
        relation = "same_lexeme_candidate"
    elif quantities["agreement_positions"] and not quantities["conflict_positions"]:
        relation = "quantity_supported_candidate"
    elif (
        current_overlap
        and len(left.current_match_ids) == 1
        and len(right.current_match_ids) == 1
    ):
        relation = "shared_words_target_candidate"
    elif semantic["status"] == "no_shared_language":
        relation = "insufficient_cross_language_evidence"
    else:
        relation = "unresolved"
    embedding = (
        embedding_evidence.comparison(left.ref, right.ref, packet_revision)
        if embedding_evidence is not None else None
    )
    if (
        embedding is not None
        and embedding.get("calibrated_support")
        and not same_family
        and not gender_conflict
        and not quantities["conflict_positions"]
        and relation not in {"strong_same_lexeme_candidate", "same_lexeme_candidate"}
    ):
        relation = "embedding_same_lexeme_candidate"
    ranking = score
    ranking += min(0.08, 0.02 * len(quantities["agreement_positions"]))
    ranking += 0.05 if current_overlap else 0.0
    ranking += 0.02 if left.gender and left.gender == right.gender else 0.0
    ranking -= 0.15 if gender_conflict else 0.0
    if embedding is not None and embedding.get("calibrated_support"):
        ranking = max(ranking, float(embedding["max_cosine"]))
    result = {
        "left": left.ref,
        "right": right.ref,
        "relation": relation,
        "ranking_score": round(max(0.0, ranking), 6),
        "semantic": semantic,
        "structure": {
            "left_part_of_speech": left.part,
            "right_part_of_speech": right.part,
            "left_gender": left.gender,
            "right_gender": right.gender,
            "gender_conflict": gender_conflict,
            "shared_current_words_targets": current_overlap,
            "quantity": quantities,
        },
        "automatic_acceptance_allowed": False,
    }
    if embedding is not None:
        result["embedding_evidence"] = embedding
    return result


def effective_parts(nodes: tuple[Node, ...]) -> dict[str, list[Node]]:
    known = sorted({node.part for node in nodes if node.part is not None})
    if not known:
        return {"UNKNOWN": list(nodes)}
    return {
        part: [node for node in nodes if node.part in {None, part}]
        for part in known
    }


def field_consensus(nodes: list[Node], field: str) -> dict[str, Any]:
    votes = {
        node.family: getattr(node, field)
        for node in nodes
        if node.primary and getattr(node, field) is not None
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


def quantity_consensus(nodes: list[Node]) -> list[dict[str, Any]]:
    by_position: dict[int, dict[str, str]] = defaultdict(dict)
    for node in nodes:
        if not node.primary:
            continue
        for position, value in node.quantity:
            by_position[position][node.family] = value
    output = []
    for position, votes in sorted(by_position.items()):
        counts = Counter(votes.values())
        winners = [value for value, count in counts.items() if count >= 2]
        if len(winners) == 1:
            status, value = "majority_2_of_3", winners[0]
        elif len(counts) <= 1:
            status, value = "agreement_without_quorum", next(iter(counts))
        else:
            status, value = "conflict", None
        output.append({"position": position, "status": status, "value": value, "votes": dict(sorted(votes.items()))})
    return output


def propose_components(
    part: str, nodes: list[Node], pairs: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    by_ref = {node.ref: node for node in nodes}
    families = {node.ref: node.family for node in nodes}
    union = UnionFind(by_ref, families)
    collisions: list[dict[str, Any]] = []
    semantic_relations = {
        "strong_same_lexeme_candidate",
        "same_lexeme_candidate",
        "embedding_same_lexeme_candidate",
    }

    def edge_score(candidate: dict[str, Any]) -> float:
        embedding = candidate.get("embedding_evidence", {})
        if embedding.get("calibrated_support"):
            return max(
                float(candidate["semantic"]["best_score"]),
                float(embedding["max_cosine"]),
            )
        return float(candidate["semantic"]["best_score"])

    def competing_scores(node_ref: str, other_family: str) -> list[float]:
        values = []
        for candidate in pairs:
            if candidate["relation"] not in semantic_relations:
                continue
            if candidate["left"] == node_ref:
                other_ref = candidate["right"]
            elif candidate["right"] == node_ref:
                other_ref = candidate["left"]
            else:
                continue
            if by_ref[other_ref].family == other_family:
                values.append(edge_score(candidate))
        return sorted(values, reverse=True)

    selected = []
    for pair in pairs:
        if pair["relation"] not in semantic_relations:
            continue
        left, right = by_ref[pair["left"]], by_ref[pair["right"]]
        score = edge_score(pair)
        left_scores = competing_scores(left.ref, right.family)
        right_scores = competing_scores(right.ref, left.family)
        left_unique = left_scores and score == left_scores[0] and left_scores.count(score) == 1
        right_unique = right_scores and score == right_scores[0] and right_scores.count(score) == 1
        left_margin = score - (left_scores[1] if len(left_scores) > 1 else 0.0)
        right_margin = score - (right_scores[1] if len(right_scores) > 1 else 0.0)
        if pair["relation"] == "embedding_same_lexeme_candidate":
            embedding = pair["embedding_evidence"]
            required_margin = float(
                min(embedding["left_margin"], embedding["right_margin"])
            )
        else:
            required_margin = 0.0 if pair["relation"] == "strong_same_lexeme_candidate" else 0.025
        accepted = bool(
            left_unique
            and right_unique
            and left_margin >= required_margin
            and right_margin >= required_margin
        )
        pair["alignment_edge"] = {
            "status": "selected_mutual_unique" if accepted else "rejected_ambiguous_or_low_margin",
            "left_margin": round(left_margin, 6),
            "right_margin": round(right_margin, 6),
            "required_margin": required_margin,
        }
        if accepted:
            selected.append(pair)

    selected.sort(
        key=lambda pair: (-pair["ranking_score"], pair["left"], pair["right"])
    )
    accepted_relations: dict[tuple[str, str], str] = {}
    for pair in selected:
        if not union.union_without_family_collision(pair["left"], pair["right"]):
            collisions.append({"left": pair["left"], "right": pair["right"], "reason": "source_family_collision"})
            pair["alignment_edge"]["status"] = "rejected_source_family_collision"
            continue
        accepted_relations[tuple(sorted((pair["left"], pair["right"])))] = pair["relation"]
    grouped: dict[str, list[Node]] = defaultdict(list)
    for node in nodes:
        grouped[union.find(node.ref)].append(node)
    components = []
    for index, members in enumerate(
        sorted(grouped.values(), key=lambda group: tuple(sorted(node.ref for node in group))), 1
    ):
        refs = sorted(node.ref for node in members)
        primary_count = sum(node.primary for node in members)
        member_refs = set(refs)
        used_relations = {
            relation
            for edge, relation in accepted_relations.items()
            if set(edge) <= member_refs
        }
        if len(members) == 1:
            basis = "unassigned_singleton"
        elif used_relations == {"strong_same_lexeme_candidate"}:
            basis = "strong_mutual_semantic_edges"
        elif used_relations == {"embedding_same_lexeme_candidate"}:
            basis = "calibrated_embedding_edges"
        else:
            basis = "mutual_semantic_edges"
        components.append(
            {
                "component_id": f"{part.lower()}-{index}",
                "basis": basis,
                "members": refs,
                "primary_family_count": primary_count,
                "consensus_preview": {
                    "gender": field_consensus(members, "gender"),
                    "indeclinable": field_consensus(members, "indeclinable"),
                    "vowel_quantity": quantity_consensus(members),
                },
                "automatic_acceptance_allowed": False,
            }
        )
    return components + ([{"component_id": "collision-report", "basis": "rejected_strong_edges", "collisions": collisions, "members": [], "automatic_acceptance_allowed": False}] if collisions else [])


def node_summary(node: Node, inferred_part: str) -> dict[str, Any]:
    return {
        "ref": node.ref,
        "source": node.source,
        "source_family": node.family,
        "primary_consensus_authority": node.primary,
        "part_of_speech": node.part,
        "effective_part_of_speech": inferred_part,
        "part_of_speech_inferred_for_analysis": node.part is None,
        "gender": node.gender,
        "indeclinable": node.indeclinable,
        "homograph_number": node.homograph_number,
        "current_words_match_ids": list(node.current_match_ids),
        "quantity_observations": [
            {"position": position, "quantity": value}
            for position, value in node.quantity
        ],
        "label": node.label,
        "semantic_sections": [
            {"section_ref": section.section_ref, "language": section.language, "preview": section.text[:240]}
            for section in node.sections
        ],
    }


def analysis_revision(record: dict[str, Any]) -> str:
    payload = json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "sha256:" + hashlib.sha256(payload.encode("utf-8")).hexdigest()


def analyze_packet(
    packet: dict[str, Any], frequency: Counter[tuple[str, str]],
    documents: Counter[str], strong_score: float, candidate_score: float,
    embedding_evidence: EmbeddingEvidence | None = None,
) -> dict[str, Any] | None:
    nodes = packet_nodes(packet)
    parts = effective_parts(nodes)
    analyses = []
    for part, members in sorted(parts.items()):
        if len(members) < 2:
            continue
        pairs = [
            pair_record(
                left, right, frequency, documents, strong_score, candidate_score,
                embedding_evidence, packet["revision"],
            )
            for left, right in itertools.combinations(sorted(members, key=lambda node: node.ref), 2)
        ]
        analyses.append(
            {
                "part_of_speech": part,
                "nodes": [node_summary(node, part) for node in sorted(members, key=lambda node: node.ref)],
                "pair_candidates": pairs,
                "proposed_components": propose_components(part, members, pairs),
            }
        )
    if not analyses:
        return None
    result = {
        "schema": OUTPUT_SCHEMA_V2 if embedding_evidence is not None else OUTPUT_SCHEMA,
        "algorithm": ALGORITHM_V2 if embedding_evidence is not None else ALGORITHM,
        "packet_key": packet["key"],
        "packet_revision": packet["revision"],
        "analysis": analyses,
        "decision": {
            "status": "needs_review",
            "required": "confirm component membership by meaning before applying consensus",
            "automatic_promotion_allowed": False,
        },
    }
    result["revision"] = analysis_revision(result)
    return result


def run(arguments: argparse.Namespace) -> dict[str, Any]:
    frequency, documents = collect_document_frequency(arguments.input)
    embedding_evidence = (
        EmbeddingEvidence(
            arguments.embedding_evidence, arguments.embedding_run_id,
            arguments.embedding_calibration,
        )
        if getattr(arguments, "embedding_evidence", None) is not None else None
    )
    counts = Counter()
    relation_counts = Counter()
    component_counts = Counter()
    edge_counts = Counter()
    with open_text(arguments.output, "w") as output:
        for packet in read_packets(arguments.input):
            counts["input_packets"] += 1
            result = analyze_packet(
                packet, frequency, documents,
                arguments.strong_score, arguments.candidate_score,
                embedding_evidence,
            )
            if result is None:
                counts["skipped_without_comparable_entries"] += 1
                continue
            counts["output_records"] += 1
            for analysis in result["analysis"]:
                counts["part_queues"] += 1
                relation_counts.update(pair["relation"] for pair in analysis["pair_candidates"])
                edge_counts.update(
                    pair["alignment_edge"]["status"]
                    for pair in analysis["pair_candidates"]
                    if "alignment_edge" in pair
                )
                for component in analysis["proposed_components"]:
                    component_counts[component["basis"]] += 1
                    members = component.get("members", [])
                    if not members:
                        continue
                    counts["proposed_components"] += 1
                    if len(members) > 1:
                        counts["proposed_multi_entry_components"] += 1
                        counts["members_in_multi_entry_components"] += len(members)
                        if any(member.startswith("words:") for member in members):
                            counts["multi_entry_components_with_words_target"] += 1
            output.write(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n")
    report = {
        "schema": REPORT_SCHEMA_V2 if embedding_evidence is not None else REPORT_SCHEMA,
        "algorithm": ALGORITHM_V2 if embedding_evidence is not None else ALGORITHM,
        "input": str(arguments.input),
        "output": str(arguments.output),
        "thresholds": {"candidate": arguments.candidate_score, "strong": arguments.strong_score},
        "document_sections_by_language": dict(sorted(documents.items())),
        "counts": dict(sorted(counts.items())),
        "pair_relations": dict(sorted(relation_counts.items())),
        "alignment_edges": dict(sorted(edge_counts.items())),
        "component_bases": dict(sorted(component_counts.items())),
        "policy": {
            "cross_language_without_textual_bridge": "insufficient_evidence",
            "same_source_family_merge": "forbidden_in_proposed_components",
            "automatic_promotion_allowed": False,
        },
    }
    if embedding_evidence is not None:
        report["embedding_evidence"] = {
            "run_id": embedding_evidence.run_id,
            "model": embedding_evidence.model,
            "model_digest": embedding_evidence.model_digest,
            "calibrated_for_component_proposals": embedding_evidence.calibration is not None,
        }
    return report


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="comparison JSONL or JSONL.GZ")
    parser.add_argument("--output", type=Path, required=True, help="alignment review JSONL or JSONL.GZ")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--candidate-score", type=float, default=0.10)
    parser.add_argument("--strong-score", type=float, default=0.22)
    parser.add_argument("--embedding-evidence", type=Path, help="optional scored evidence SQLite")
    parser.add_argument("--embedding-run-id")
    parser.add_argument("--embedding-calibration", type=Path, help="optional passing calibration JSON")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if not 0.0 <= arguments.candidate_score < arguments.strong_score <= 1.0:
        raise AlignmentError("thresholds must satisfy 0 <= candidate < strong <= 1")
    if arguments.embedding_calibration is not None and arguments.embedding_evidence is None:
        raise AlignmentError("--embedding-calibration requires --embedding-evidence")
    report = run(arguments)
    arguments.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {report['counts'].get('output_records', 0)} semantic review records to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
