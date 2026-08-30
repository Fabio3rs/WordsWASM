#!/usr/bin/env python3

"""Suggest review-only stem quantity evidence from established dictionaries."""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import re
import sqlite3
import unicodedata
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Iterable, Iterator


DICTIONARY_RECORD_SIZE = 180
STEM_SIZE = 18
STEM_SLOTS = 4
MEANING_OFFSET = 97
MEANING_SIZE = 80
MACRON = "\N{COMBINING MACRON}"
BREVE = "\N{COMBINING BREVE}"
QUANTITY_MARKS = frozenset({MACRON, BREVE})
SOURCE_EVIDENCE_IDS = {
    "ls_dict": "lewis-short-ls-dict",
    "gaffiot": "gaffiot",
    "collatinus": "collatinus-derived-lexicon",
    "latin_german": "latin-german-morphological-lexicon",
    "faria_v3": "faria-v3-quality",
}
SQLITE_SOURCE_NAMES = frozenset({"ls_dict", "gaffiot"})
SOURCE_FAMILIES = {
    "ls_dict": ("lewis", True),
    "gaffiot": ("gaffiot", True),
    # WHY: Collatinus explicitly synthesizes its lexicon from several works,
    # including Lewis & Short and Gaffiot. It is useful corroboration and a
    # source of candidates, but cannot be counted as another independent vote.
    "collatinus": ("collatinus-derived", False),
    "latin_german": ("latin-german", True),
    "faria_v3": ("faria", True),
}
COLLATINUS_POS = {
    "a": "ADJ",
    "d": "ADV",
    "m": "NUM",
    "n": "NOUN",
    "p": "PRON",
    "v": "VERB",
}
PART_NAMES = {
    0: "UNKNOWN",
    1: "NOUN",
    2: "PRON",
    3: "PRON",
    4: "ADJ",
    5: "NUM",
    6: "ADV",
    7: "VERB",
    8: "VERB",
    9: "VERB",
    10: "PREP",
    11: "CONJ",
    12: "INTERJ",
}
GENDER_NAMES = {0: None, 1: "m", 2: "f", 3: "n", 4: "c"}
LATIN_TOKEN = re.compile(r"[a-z]+")
TOKEN = re.compile(r"[a-z]{3,}")
SEMANTIC_STOP_WORDS = frozenset(
    {
        "and", "are", "but", "for", "from", "has", "have", "into", "not",
        "one", "that", "the", "their", "this", "through", "with",
        "aux", "avec", "dans", "des", "est", "les", "par", "pour", "que",
        "qui", "une",
    }
)
CITATION_ENDINGS = {
    "NOUN": frozenset({"", "a", "ae", "e", "es", "i", "is", "o", "on", "os", "s", "u", "um", "us"}),
    "PRON": frozenset({"", "a", "ae", "e", "i", "is", "o", "um", "us"}),
    "ADJ": frozenset({"", "a", "e", "er", "is", "ns", "um", "us"}),
    "NUM": frozenset({"", "a", "ae", "i", "um", "us"}),
    "ADV": frozenset({""}),
    "VERB": frozenset({"", "i", "io", "ior", "o", "or"}),
    "PREP": frozenset({""}),
    "CONJ": frozenset({""}),
    "INTERJ": frozenset({""}),
}


class SuggestionError(ValueError):
    """An input cannot safely participate in quantity suggestion."""


@dataclass(frozen=True)
class MarkedWord:
    source: str
    marks: tuple[str | None, ...]
    proper: bool

    def marked_prefix(self, length: int) -> str:
        pieces: list[str] = []
        for letter, mark in zip(self.source[:length], self.marks[:length]):
            pieces.append(letter)
            if mark is not None:
                pieces.append(mark)
        return unicodedata.normalize("NFC", "".join(pieces))

    def prefix_has_quantity(self, length: int) -> bool:
        return any(mark is not None for mark in self.marks[:length])


@dataclass(frozen=True)
class WhitakerStem:
    dictionary_entry: int
    slot: int
    stem: str
    proper: bool
    part_of_speech: str
    gender: str | None
    meaning: str
    declension: int = 0
    variant: int = 0


@dataclass(frozen=True)
class DictionaryEntry:
    source_name: str
    source_entry_id: str
    lemma: str
    part_of_speech: str | None
    gender: str | None
    head: str
    source_family: str = ""
    independent_quantity_authority: bool = True
    morphology_hint: str | None = None
    morphology_key: str | None = None
    external_stems: tuple[tuple[int, str], ...] = ()


@dataclass(frozen=True)
class CollatinusModel:
    part_of_speech: str | None
    radical_rules: tuple[tuple[int, str], ...]


@dataclass(frozen=True)
class Candidate:
    dictionary: DictionaryEntry
    whitaker: WhitakerStem
    word: MarkedWord
    semantic_overlap: tuple[str, ...]
    alternatives: int = 0

    def evidence_id(self) -> str:
        locator = re.sub(r"[^a-z0-9]+", "-", self.dictionary.source_entry_id.lower())
        digest = hashlib.sha256(self.dictionary.source_entry_id.encode("utf-8")).hexdigest()[:8]
        return (
            f"candidate-{self.dictionary.source_name}-{locator}-{digest}-"
            f"e{self.whitaker.dictionary_entry}-s{self.whitaker.slot}"
        )

    def evidence_record(self) -> dict[str, Any]:
        suffix = self.word.source[len(self.whitaker.stem) :]
        overlap = ", ".join(self.semantic_overlap) or "none"
        note = (
            "Generated review-only candidate: marked dictionary lemma has an exact "
            f"Whitaker stem prefix; suffix={suffix!r}; semantic_overlap={overlap}; "
            f"alternatives_for_witness={self.alternatives}."
        )
        return {
            "record": "evidence",
            "id": self.evidence_id(),
            "target": {
                "kind": "stem",
                "dictionary_entry": self.whitaker.dictionary_entry,
                "slot": self.whitaker.slot,
            },
            "base": self.whitaker.stem,
            "marked": self.word.marked_prefix(len(self.whitaker.stem)),
            "source": SOURCE_EVIDENCE_IDS[self.dictionary.source_name],
            "locator": f"entry {self.dictionary.source_entry_id}",
            "witness": short_witness(self.dictionary.head, self.dictionary.lemma),
            "confidence": "needs_review",
            "label": f"candidate for {self.dictionary.lemma}",
            "note": note,
        }

    def report_record(self, *, quantity_conflict: bool = False) -> dict[str, Any]:
        record = self.evidence_record()
        return {
            "id": record["id"],
            "target": record["target"],
            "base": record["base"],
            "marked": record["marked"],
            "source": record["source"],
            "source_family": self.dictionary.source_family,
            "independent_quantity_authority": (
                self.dictionary.independent_quantity_authority
            ),
            "locator": record["locator"],
            "external_lemma": self.dictionary.lemma,
            "external_is_proper": self.word.proper,
            "external_part_of_speech": self.dictionary.part_of_speech,
            "external_gender": self.dictionary.gender,
            "external_morphology_hint": self.dictionary.morphology_hint,
            "whitaker_part_of_speech": self.whitaker.part_of_speech,
            "whitaker_is_proper": self.whitaker.proper,
            "whitaker_gender": self.whitaker.gender,
            "whitaker_meaning": self.whitaker.meaning,
            "semantic_overlap": list(self.semantic_overlap),
            "alternatives_for_witness": self.alternatives,
            "quantity_conflict": quantity_conflict,
        }

    def witness_key(self) -> tuple[str, str]:
        return (self.dictionary.source_name, self.dictionary.source_entry_id)

    def target_key(self) -> tuple[int, int]:
        return (self.whitaker.dictionary_entry, self.whitaker.slot)

    def homograph_key(self) -> tuple[str, str]:
        # WHY: only identical citation words after removing quantity can be
        # disambiguated by restoring that quantity. A shared stem alone would
        # incorrectly prioritize unrelated words with different endings.
        return (self.dictionary.source_name, self.word.source)

    def quantity_marks(self) -> tuple[tuple[int, str], ...]:
        return tuple(
            (position, mark)
            for position, mark in enumerate(self.word.marks[: len(self.whitaker.stem)])
            if mark is not None
        )


@dataclass(frozen=True)
class ExistingQuantityVote:
    target: tuple[int, int]
    base: str
    position: int
    mark: str
    source_id: str
    source_family: str
    independent_quantity_authority: bool
    witness: str


def decode_ascii(field: bytes, context: str) -> str:
    try:
        return field.decode("ascii").strip()
    except UnicodeDecodeError as error:
        raise SuggestionError(f"{context}: DICTFILE.GEN is not ASCII") from error


def read_whitaker_stems(path: Path) -> dict[str, tuple[WhitakerStem, ...]]:
    data = path.read_bytes()
    if len(data) % DICTIONARY_RECORD_SIZE != 0:
        raise SuggestionError("DICTFILE.GEN size is not a multiple of 180 bytes")

    by_stem: dict[str, list[WhitakerStem]] = {}
    for record_index in range(len(data) // DICTIONARY_RECORD_SIZE):
        begin = record_index * DICTIONARY_RECORD_SIZE
        record = data[begin : begin + DICTIONARY_RECORD_SIZE]
        part_number = record[72]
        declension = int.from_bytes(record[76:80], "little")
        variant = int.from_bytes(record[80:84], "little")
        gender_number = record[84] if part_number == 1 else 0
        part = PART_NAMES.get(part_number, "OTHER")
        gender = GENDER_NAMES.get(gender_number)
        meaning = decode_ascii(
            record[MEANING_OFFSET : MEANING_OFFSET + MEANING_SIZE],
            f"dictionary entry {record_index + 1}",
        )
        for slot_index in range(STEM_SLOTS):
            offset = slot_index * STEM_SIZE
            source_stem = decode_ascii(
                record[offset : offset + STEM_SIZE],
                f"dictionary entry {record_index + 1} slot {slot_index + 1}",
            )
            stem = source_stem.lower()
            # WHY: quantity suggestion currently examines prefixes of at least
            # three letters, but lexical coverage also needs Words' short
            # function words. Keep the shared reader lossless and let each
            # consumer impose its own safe matching threshold.
            if stem == "zzz" or LATIN_TOKEN.fullmatch(stem) is None:
                continue
            item = WhitakerStem(
                record_index + 1,
                slot_index + 1,
                stem,
                source_stem[:1].isupper(),
                part,
                gender,
                meaning,
                declension,
                variant,
            )
            by_stem.setdefault(stem, []).append(item)
    return {stem: tuple(items) for stem, items in by_stem.items()}


def extract_first_word(
    lemma: str, *, require_quantity: bool = True
) -> MarkedWord | None:
    # WHY: Gaffiot prefixes many homographs with an ordinal ("3 ā, ăb, abs").
    # Selecting the first alphabetic run retains the actual headword without
    # pretending that alternate spellings are one continuous lemma.
    start = next((index for index, char in enumerate(lemma) if char.isalpha()), None)
    if start is None:
        return None
    end = start
    while end < len(lemma):
        char = lemma[end]
        if not char.isalpha() and unicodedata.combining(char) == 0:
            break
        end += 1

    # WHY: an editorial marker inside a word is not a Latin boundary. Treating
    # cŏrō^na as cŏrō created a false match with an unrelated cor- lexeme.
    if end < len(lemma) and lemma[end] in {"^", "-", "_"}:
        continuation = end + 1
        if continuation < len(lemma) and lemma[continuation].isalpha():
            return None
    if end < len(lemma) and lemma[end].isspace():
        continuation = end
        while continuation < len(lemma) and lemma[continuation].isspace():
            continuation += 1
        if continuation < len(lemma) and lemma[continuation].isalpha():
            return None

    letters: list[str] = []
    marks: list[str | None] = []
    for char in unicodedata.normalize("NFD", lemma[start:end].lower()):
        if unicodedata.combining(char) == 0:
            if char < "a" or char > "z":
                return None
            letters.append(char)
            marks.append(None)
            continue
        if char not in QUANTITY_MARKS or not marks or marks[-1] is not None:
            return None
        marks[-1] = char
    if not letters or (require_quantity and not any(mark is not None for mark in marks)):
        return None
    return MarkedWord("".join(letters), tuple(marks), lemma[start].isupper())


def sqlite_uri(path: Path) -> str:
    # WHY: immutable mode prevents journal creation next to read-only reference
    # dictionaries and documents that the generator must never edit them.
    return f"file:{path.resolve().as_posix()}?immutable=1"


def read_collatinus_models(path: Path) -> dict[str, CollatinusModel]:
    """Resolve Collatinus model inheritance to the POS needed for matching."""

    definitions: dict[
        str, tuple[str | None, str | None, tuple[tuple[int, str], ...]]
    ] = {}
    current_name: str | None = None
    current_parent: str | None = None
    current_pos: str | None = None
    current_radicals: dict[int, str] = {}

    def finish() -> None:
        nonlocal current_name, current_parent, current_pos, current_radicals
        if current_name is None:
            return
        if current_name in definitions:
            # WHY: the shipped Collatinus 11 table repeats a small number of
            # models in separate sections. For POS inheritance those repeated
            # definitions are equivalent, so rejecting them would make the
            # metadata reader stricter than the Collatinus runtime.
            value = (current_parent, current_pos, tuple(sorted(current_radicals.items())))
            if definitions[current_name] != value:
                raise SuggestionError(
                    f"{path}: conflicting duplicate model {current_name!r}"
                )
        else:
            definitions[current_name] = (
                current_parent,
                current_pos,
                tuple(sorted(current_radicals.items())),
            )
        current_name = None
        current_parent = None
        current_pos = None
        current_radicals = {}

    for line_number, source_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = source_line.strip()
        if not line or line.startswith("!") or line.startswith("$"):
            continue
        key, separator, value = line.partition(":")
        if not separator:
            raise SuggestionError(f"{path}:{line_number}: malformed model directive")
        value = value.strip()
        if key == "modele":
            finish()
            if not value:
                raise SuggestionError(f"{path}:{line_number}: empty model name")
            current_name = value
        elif key == "pere":
            if current_name is None or current_parent is not None:
                raise SuggestionError(f"{path}:{line_number}: misplaced model parent")
            current_parent = value
        elif key == "pos":
            if current_name is None or current_pos is not None:
                raise SuggestionError(f"{path}:{line_number}: misplaced model POS")
            if value not in COLLATINUS_POS:
                raise SuggestionError(
                    f"{path}:{line_number}: unsupported Collatinus POS {value!r}"
                )
            current_pos = COLLATINUS_POS[value]
        elif key == "R":
            if current_name is None:
                raise SuggestionError(f"{path}:{line_number}: radical without model")
            number_text, separator, rule = value.partition(":")
            if not separator or not number_text.isdecimal() or not rule:
                raise SuggestionError(f"{path}:{line_number}: malformed radical rule")
            number = int(number_text)
            if number in current_radicals:
                raise SuggestionError(
                    f"{path}:{line_number}: duplicate radical {number}"
                )
            current_radicals[number] = rule
    finish()

    resolved: dict[str, CollatinusModel] = {}
    active: set[str] = set()

    def resolve(name: str) -> CollatinusModel:
        if name in resolved:
            return resolved[name]
        if name in active:
            raise SuggestionError(f"{path}: model inheritance cycle at {name!r}")
        try:
            parent, own_pos, own_radicals = definitions[name]
        except KeyError as error:
            raise SuggestionError(f"{path}: unknown model {name!r}") from error
        active.add(name)
        inherited = resolve(parent) if parent is not None else CollatinusModel(None, ())
        active.remove(name)
        radicals = dict(inherited.radical_rules)
        radicals.update(own_radicals)
        resolved[name] = CollatinusModel(
            own_pos if own_pos is not None else inherited.part_of_speech,
            tuple(sorted(radicals.items())),
        )
        return resolved[name]

    for model_name in definitions:
        resolve(model_name)
    return resolved


def collatinus_lookup_key(value: str) -> str:
    # WHY: translations use an ASCII-like lookup key while lemmes.la keeps
    # quantity marks. Removing combining marks preserves the numeric homograph
    # discriminator (for example levis2) without making it part of the lemma.
    return "".join(
        character
        for character in unicodedata.normalize("NFD", value)
        if unicodedata.combining(character) == 0
    )


def read_collatinus_translations(path: Path) -> dict[str, str]:
    translations: dict[str, str] = {}
    for line_number, source_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = source_line.strip()
        if not line or line.startswith("!"):
            continue
        key, separator, translation = line.partition(":")
        # The distributed translation files carry one un-commented language
        # label (for example "English") before their keyed records.
        if not separator and not translations:
            continue
        if not separator or not key:
            raise SuggestionError(f"{path}:{line_number}: malformed translation")
        value = translation.strip()
        if key in translations:
            # Exact duplicate translation rows exist in the distributed data.
            # Some repeated keys contain complementary glosses; preserving a
            # sorted union keeps all review terms without making output depend
            # on which duplicate appeared first.
            translations[key] = " | ".join(
                sorted({translations[key], value})
            )
            continue
        translations[key] = value
    return translations


def collatinus_gender(information: str) -> str | None:
    match = re.search(r"\b([mfnc])\.", information.lower())
    return match.group(1) if match is not None else None


def latin_ascii(value: str) -> str | None:
    letters = "".join(
        character
        for character in unicodedata.normalize("NFD", value).lower()
        if unicodedata.combining(character) == 0
    )
    return letters if LATIN_TOKEN.fullmatch(letters) is not None else None


def collatinus_radicals(
    canonical: str,
    model: CollatinusModel,
    first_stem: str,
    second_stem: str,
) -> tuple[tuple[int, str], ...]:
    canonical_ascii = latin_ascii(canonical)
    if canonical_ascii is None:
        return ()
    radicals: dict[int, str] = {}
    for number, field in ((1, first_stem), (2, second_stem)):
        if not field:
            continue
        alternatives = {
            stem
            for value in field.split(",")
            if (stem := latin_ascii(value.strip())) is not None
        }
        if len(alternatives) == 1:
            radicals[number] = next(iter(alternatives))
    for number, rule in model.radical_rules:
        if number in radicals or rule == "-":
            continue
        if rule == "K":
            radicals[number] = canonical_ascii
            continue
        remove_text, separator, addition = rule.partition(",")
        if not separator or not remove_text.isdecimal():
            continue
        remove = int(remove_text)
        if remove > len(canonical_ascii):
            continue
        addition_ascii = "" if addition == "0" else latin_ascii(addition)
        if addition_ascii is None:
            continue
        radicals[number] = canonical_ascii[: len(canonical_ascii) - remove] + addition_ascii
    return tuple(sorted(radicals.items()))


def read_collatinus_lemma_file(
    path: Path,
    models: dict[str, CollatinusModel],
    translations: dict[str, str],
) -> Iterator[DictionaryEntry]:
    seen_ids: set[str] = set()
    for line_number, source_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = source_line.strip()
        if not line or line.startswith("!"):
            continue
        fields = line.split("|")
        if len(fields) != 6:
            raise SuggestionError(
                f"{path}:{line_number}: expected six Collatinus fields"
            )
        headword, model, first_stem, second_stem, information, frequency = fields
        # Collatinus permits a trailing editorial comment after the frequency
        # field, as in the shipped confīgo record. An exclamation mark may also
        # be the actual headword (`ho!`), so only the final field is trimmed.
        frequency = frequency.partition("!")[0].strip()
        if not headword or not model or (frequency and not frequency.isdecimal()):
            raise SuggestionError(f"{path}:{line_number}: malformed Collatinus lemma")
        if model not in models:
            raise SuggestionError(
                f"{path}:{line_number}: unknown Collatinus model {model!r}"
            )

        lookup_source, separator, displayed = headword.partition("=")
        lookup_key = collatinus_lookup_key(lookup_source)
        # The trailing number is a Collatinus homograph discriminator. The
        # right side of '=' already contains the display form without it.
        primary = displayed.split(",", maxsplit=1)[0] if separator else re.sub(
            r"\d+$", "", lookup_source
        )
        primary = primary.strip()
        if not primary:
            raise SuggestionError(f"{path}:{line_number}: empty display lemma")

        source_entry_id = f"{path.name}:{lookup_key}"
        if source_entry_id in seen_ids:
            # The extended lexicon contains distinct records whose unmarked
            # lookup keys collide (for example Amasĭas and Amasīas). Preserve
            # both with a stable content discriminator; they remain members of
            # one derived source family and therefore are never separate votes.
            digest = hashlib.sha256(line.encode("utf-8")).hexdigest()[:8]
            source_entry_id = f"{source_entry_id}:{digest}"
            if source_entry_id in seen_ids:
                continue
        seen_ids.add(source_entry_id)
        translation = translations.get(lookup_key, "")
        head = ", ".join(piece for piece in (primary, information, translation) if piece)
        family, independent = SOURCE_FAMILIES["collatinus"]
        gender = collatinus_gender(information)
        model_record = models[model]
        part = model_record.part_of_speech
        if part is None and gender is not None:
            # An invariant Collatinus model covers several POS classes, while
            # an explicit grammatical gender identifies its nominal records.
            part = "NOUN"
        yield DictionaryEntry(
            source_name="collatinus",
            source_entry_id=source_entry_id,
            lemma=primary,
            part_of_speech=part,
            gender=gender,
            head=head,
            source_family=family,
            independent_quantity_authority=independent,
            morphology_hint="|".join((model, first_stem, second_stem, information)),
            morphology_key=f"collatinus:{model}",
            external_stems=collatinus_radicals(
                primary, model_record, first_stem, second_stem
            ),
        )


def read_collatinus_entries(
    data_directory: Path, *, include_extended: bool = False
) -> Iterator[DictionaryEntry]:
    models = read_collatinus_models(data_directory / "modeles.la")
    file_pairs = [("lemmes.la", "lemmes.en")]
    if include_extended:
        file_pairs.append(("lem_ext.la", "lem_ext.en"))
    for lemma_name, translation_name in file_pairs:
        translations = read_collatinus_translations(data_directory / translation_name)
        yield from read_collatinus_lemma_file(
            data_directory / lemma_name, models, translations
        )


def latin_german_lemma(latin: str, grammar: str) -> str:
    # WHY: verbal entries cite the infinitive before the first-person form,
    # while Whitaker slot 1 is matched from the finite citation stem. Nominal
    # entries already place their nominative citation form first.
    if grammar == "v":
        principal_parts = latin.split(",")
        if len(principal_parts) > 1:
            return principal_parts[1].strip().split(maxsplit=1)[0]
    return latin.strip().split(maxsplit=1)[0]


def latin_german_gender(latin: str) -> str | None:
    match = re.search(r"(?:,|\s)\s*([mfnc])(?:\s|$)", latin.lower())
    return match.group(1) if match is not None else None


def read_latin_german_entries(database: Path) -> Iterator[DictionaryEntry]:
    connection = sqlite3.connect(sqlite_uri(database), uri=True)
    try:
        columns = {row[1] for row in connection.execute("pragma table_info(VOC)")}
        required = {"id", "vok_id", "latin", "desc", "grammar", "typnr"}
        if not required <= columns:
            missing = ", ".join(sorted(required - columns))
            raise SuggestionError(f"VOC table is missing columns: {missing}")
        parts = {"a": "ADJ", "s": "NOUN", "v": "VERB"}
        query = """
            select id, vok_id, latin, desc, grammar, typnr
              from VOC
             where grammar in ('a', 's', 'v')
             order by id
        """
        for row in connection.execute(query):
            grammar = str(row[4])
            latin = str(row[2] or "").strip()
            lemma = latin_german_lemma(latin, grammar)
            if not lemma:
                continue
            family, independent = SOURCE_FAMILIES["latin_german"]
            description = str(row[3] or "").strip()
            head = "; ".join(piece for piece in (latin, description) if piece)
            yield DictionaryEntry(
                source_name="latin_german",
                source_entry_id=str(row[1] or f"VOC:{row[0]}"),
                lemma=lemma,
                part_of_speech=parts[grammar],
                gender=latin_german_gender(latin) if grammar == "s" else None,
                head=head,
                source_family=family,
                independent_quantity_authority=independent,
                morphology_hint=f"typnr:{row[5]};citation:{latin}",
                morphology_key=f"latin-german:{row[5]}",
            )
    finally:
        connection.close()


def read_dictionary_entries(
    database: Path, source_names: Iterable[str]
) -> Iterator[DictionaryEntry]:
    names = tuple(dict.fromkeys(source_names))
    unsupported = sorted(set(names) - SQLITE_SOURCE_NAMES)
    if unsupported:
        raise SuggestionError(f"unsupported source(s): {', '.join(unsupported)}")
    connection = sqlite3.connect(sqlite_uri(database), uri=True)
    try:
        columns = {row[1] for row in connection.execute("pragma table_info(entry)")}
        required = {
            "source_id",
            "source_entry_id",
            "lemma",
            "pos_std",
            "gender_std",
            "head_raw",
        }
        if not required <= columns:
            missing = ", ".join(sorted(required - columns))
            raise SuggestionError(f"entry table is missing columns: {missing}")
        placeholders = ",".join("?" for _ in names)
        query = f"""
            select source.name, entry.source_entry_id, entry.lemma,
                   entry.pos_std, entry.gender_std, entry.head_raw
              from entry join source on source.id = entry.source_id
             where source.name in ({placeholders})
             order by source.name, entry.source_entry_id, entry.id
        """
        for row in connection.execute(query, names):
            gender = str(row[4]).lower() if row[4] else None
            part = str(row[3]).upper() if row[3] else None
            # A recorded gender is itself strong evidence of a nominal entry.
            # This recovers many LS/Gaffiot nouns whose migrated POS is empty.
            if part in {None, "OTHER"} and gender is not None:
                part = "NOUN"
            family, independent = SOURCE_FAMILIES[str(row[0])]
            yield DictionaryEntry(
                source_name=str(row[0]),
                source_entry_id=str(row[1]),
                lemma=str(row[2]),
                part_of_speech=part,
                gender=gender,
                head=str(row[5] or row[2]),
                source_family=family,
                independent_quantity_authority=independent,
            )
    finally:
        connection.close()


def parts_compatible(external: str | None, whitaker: str) -> bool:
    return external in {None, "OTHER"} or external == whitaker


def genders_compatible(external: str | None, whitaker: str | None) -> bool:
    return external is None or whitaker is None or external == whitaker


def semantic_tokens(text: str) -> frozenset[str]:
    normalized = unicodedata.normalize("NFKD", text.lower())
    ascii_text = "".join(char for char in normalized if ord(char) < 128)
    return frozenset(TOKEN.findall(ascii_text)) - SEMANTIC_STOP_WORDS


def semantic_overlap(entry: DictionaryEntry, stem: WhitakerStem) -> tuple[str, ...]:
    # This signal is intentionally ranking metadata, never an acceptance rule:
    # Gaffiot is French and even same-language dictionary glosses need not agree.
    excluded = semantic_tokens(entry.lemma) | semantic_tokens(stem.stem)
    overlap = (semantic_tokens(entry.head) & semantic_tokens(stem.meaning)) - excluded
    return tuple(sorted(overlap))


def citation_ending_matches(word: MarkedWord, stem: WhitakerStem) -> bool:
    endings = CITATION_ENDINGS.get(stem.part_of_speech, frozenset())
    return word.source[len(stem.stem) :] in endings


def suggest(
    stems: dict[str, tuple[WhitakerStem, ...]],
    entries: Iterable[DictionaryEntry],
    *,
    maximum_ending: int = 6,
) -> tuple[Candidate, ...]:
    candidates: list[Candidate] = []
    for entry in entries:
        word = extract_first_word(entry.lemma)
        if word is None:
            continue
        local: list[Candidate] = []
        minimum = max(3, len(word.source) - maximum_ending)
        for length in range(minimum, min(STEM_SIZE, len(word.source)) + 1):
            if not word.prefix_has_quantity(length):
                continue
            for stem in stems.get(word.source[:length], ()):
                # WHY: a dictionary headword describes the citation stem. Other
                # principal-part slots require parsing the cited forms, not a
                # coincidental prefix match against the first word.
                if (
                    stem.slot != 1
                    or word.proper != stem.proper
                    or not citation_ending_matches(word, stem)
                ):
                    continue
                if not parts_compatible(entry.part_of_speech, stem.part_of_speech):
                    continue
                if not genders_compatible(entry.gender, stem.gender):
                    continue
                overlap = semantic_overlap(entry, stem)
                # If migration supplied neither POS nor gender, require an
                # English semantic signal. French-only Gaffiot entries remain
                # available once their grammatical metadata is recovered.
                if entry.part_of_speech in {None, "OTHER"} and entry.gender is None:
                    if entry.source_name != "ls_dict" or not overlap:
                        continue
                local.append(Candidate(entry, stem, word, overlap))
        alternatives = len(local)
        candidates.extend(
            Candidate(
                candidate.dictionary,
                candidate.whitaker,
                candidate.word,
                candidate.semantic_overlap,
                alternatives,
            )
            for candidate in local
        )
    return tuple(
        sorted(
            candidates,
            key=lambda item: (
                item.dictionary.source_name,
                item.dictionary.source_entry_id,
                item.whitaker.dictionary_entry,
                item.whitaker.slot,
            ),
        )
    )


def existing_keys(path: Path | None) -> frozenset[tuple[str, str, int, int]]:
    if path is None:
        return frozenset()
    keys: set[tuple[str, str, int, int]] = set()
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SuggestionError(f"{path}:{line_number}: invalid JSON") from error
        target = record.get("target", {})
        if record.get("record") != "evidence" or target.get("kind") != "stem":
            continue
        locator = record.get("locator")
        source = record.get("source")
        entry = target.get("dictionary_entry")
        slot = target.get("slot")
        if (
            isinstance(source, str)
            and isinstance(locator, str)
            and isinstance(entry, int)
            and isinstance(slot, int)
        ):
            keys.add((source, locator, entry, slot))
            # Native locators may replace a migration key during review (as
            # with Gaffiot). Source + target is sufficient to suppress the
            # already-curated pair from a newly generated queue.
            keys.add((source, "", entry, slot))
    return frozenset(keys)


def existing_consensus_votes(path: Path | None) -> tuple[ExistingQuantityVote, ...]:
    if path is None:
        return ()
    records: list[tuple[int, dict[str, Any]]] = []
    source_families: dict[str, tuple[str, bool]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SuggestionError(f"{path}:{line_number}: invalid JSON") from error
        if not isinstance(record, dict):
            raise SuggestionError(f"{path}:{line_number}: JSON record must be an object")
        records.append((line_number, record))
        if record.get("record") != "source":
            continue
        source_id = record.get("id")
        family = record.get("family")
        independent = record.get("independent_quantity_authority")
        if (
            not isinstance(source_id, str)
            or not isinstance(family, str)
            or not family
            or not isinstance(independent, bool)
        ):
            raise SuggestionError(
                f"{path}:{line_number}: source lacks consensus family metadata"
            )
        if source_id in source_families:
            raise SuggestionError(f"{path}:{line_number}: duplicate source {source_id!r}")
        source_families[source_id] = (family, independent)

    votes: list[ExistingQuantityVote] = []
    for line_number, record in records:
        target = record.get("target", {})
        if (
            record.get("record") != "evidence"
            or record.get("confidence") != "confirmed"
            or target.get("kind") != "stem"
        ):
            continue
        source_id = record.get("source")
        if not isinstance(source_id, str) or source_id not in source_families:
            raise SuggestionError(
                f"{path}:{line_number}: confirmed source has no family classification"
            )
        entry = target.get("dictionary_entry")
        slot = target.get("slot")
        base = record.get("base")
        marked = record.get("marked")
        evidence_id = record.get("id")
        if (
            not isinstance(entry, int)
            or not isinstance(slot, int)
            or not isinstance(base, str)
            or not isinstance(marked, str)
            or not isinstance(evidence_id, str)
        ):
            raise SuggestionError(
                f"{path}:{line_number}: malformed confirmed stem evidence"
            )
        word = extract_first_word(marked)
        if word is None or word.source != base:
            raise SuggestionError(
                f"{path}:{line_number}: confirmed marked stem does not match base"
            )
        family, independent = source_families[source_id]
        votes.extend(
            ExistingQuantityVote(
                (entry, slot),
                base,
                position,
                mark,
                source_id,
                family,
                independent,
                evidence_id,
            )
            for position, mark in enumerate(word.marks)
            if mark is not None
        )
    return tuple(votes)


def remove_existing(
    candidates: Iterable[Candidate], keys: frozenset[tuple[str, str, int, int]]
) -> tuple[Candidate, ...]:
    retained = tuple(
        candidate
        for candidate in candidates
        if (
            (
                SOURCE_EVIDENCE_IDS[candidate.dictionary.source_name],
                f"entry {candidate.dictionary.source_entry_id}",
                candidate.whitaker.dictionary_entry,
                candidate.whitaker.slot,
            )
            not in keys
            and (
                SOURCE_EVIDENCE_IDS[candidate.dictionary.source_name],
                "",
                candidate.whitaker.dictionary_entry,
                candidate.whitaker.slot,
            )
            not in keys
        )
    )
    alternatives: dict[tuple[str, str], int] = {}
    for candidate in retained:
        key = candidate.witness_key()
        alternatives[key] = alternatives.get(key, 0) + 1
    return tuple(
        replace(candidate, alternatives=alternatives[candidate.witness_key()])
        for candidate in retained
    )


def short_witness(head: str, fallback: str) -> str:
    compact = " ".join(head.split()) or fallback
    if len(compact) <= 180:
        return compact
    return compact[:177].rstrip() + "..."


def render_jsonl(candidates: Iterable[Candidate]) -> str:
    return "".join(
        json.dumps(
            candidate.evidence_record(), ensure_ascii=False, separators=(",", ":")
        )
        + "\n"
        for candidate in candidates
    )


def quantity_conflict_targets(
    candidates: Iterable[Candidate],
) -> frozenset[tuple[int, int]]:
    observed: dict[tuple[int, int, int], set[str]] = {}
    for candidate in candidates:
        entry, slot = candidate.target_key()
        for position, mark in candidate.quantity_marks():
            observed.setdefault((entry, slot, position), set()).add(mark)
    return frozenset(
        (entry, slot)
        for (entry, slot, _), marks in observed.items()
        if len(marks) > 1
    )


def meaning_distinguishing_homographs(
    candidates: Iterable[Candidate],
) -> dict[tuple[str, str], dict[tuple[int, int], frozenset[int]]]:
    """Find review priorities where quantity can separate citation homographs.

    The result is deliberately evidence, not an automatic mapping: ambiguous
    candidates remain visible for editorial selection of the correct sense.
    """

    grouped: dict[tuple[str, str], list[Candidate]] = {}
    for candidate in candidates:
        grouped.setdefault(candidate.homograph_key(), []).append(candidate)

    result: dict[tuple[str, str], dict[tuple[int, int], frozenset[int]]] = {}
    for homograph_key, group in grouped.items():
        opposed_by_target: dict[tuple[int, int], set[int]] = {}
        for index, left in enumerate(group):
            left_marks = dict(left.quantity_marks())
            for right in group[index + 1 :]:
                # WHY: two candidate mappings for one dictionary witness are
                # ambiguity, not proof that Latin quantity distinguishes them.
                if (
                    left.witness_key() == right.witness_key()
                    or left.target_key() == right.target_key()
                ):
                    continue
                right_marks = dict(right.quantity_marks())
                opposed = {
                    position
                    for position in left_marks.keys() & right_marks.keys()
                    if left_marks[position] != right_marks[position]
                }
                if not opposed:
                    continue
                opposed_by_target.setdefault(left.target_key(), set()).update(opposed)
                opposed_by_target.setdefault(right.target_key(), set()).update(opposed)
        if opposed_by_target:
            result[homograph_key] = {
                target: frozenset(positions)
                for target, positions in opposed_by_target.items()
            }
    return result


def quantity_name(mark: str) -> str:
    return "long" if mark == MACRON else "short"


def quantity_consensus(
    candidates: tuple[Candidate, ...],
    existing_votes: tuple[ExistingQuantityVote, ...] = (),
) -> dict[str, Any]:
    """Summarize strict per-position votes from unambiguous mappings only."""

    observations: dict[
        tuple[int, int, int],
        dict[str, dict[str, set[tuple[str, str, bool]]]],
    ] = {}
    bases: dict[tuple[int, int], str] = {}
    excluded_ambiguous_positions = 0
    for candidate in candidates:
        marks = candidate.quantity_marks()
        if candidate.alternatives != 1:
            excluded_ambiguous_positions += len(marks)
            continue
        target = candidate.target_key()
        bases[target] = candidate.whitaker.stem
        family = candidate.dictionary.source_family
        for position, mark in marks:
            observations.setdefault((*target, position), {}).setdefault(
                family, {}
            ).setdefault(mark, set()).add(
                (
                    SOURCE_EVIDENCE_IDS[candidate.dictionary.source_name],
                    candidate.dictionary.source_entry_id,
                    candidate.dictionary.independent_quantity_authority,
                )
            )

    for vote in existing_votes:
        bases[vote.target] = vote.base
        observations.setdefault((*vote.target, vote.position), {}).setdefault(
            vote.source_family, {}
        ).setdefault(vote.mark, set()).add(
            (
                vote.source_id,
                vote.witness,
                vote.independent_quantity_authority,
            )
        )

    positions: list[dict[str, Any]] = []
    decision_counts: dict[str, int] = {}
    for (entry, slot, position), families in sorted(observations.items()):
        independent_records: list[dict[str, Any]] = []
        derived_records: list[dict[str, Any]] = []
        for family, marked_observations in sorted(families.items()):
            source_names = {
                source
                for observations_for_mark in marked_observations.values()
                for source, _, _ in observations_for_mark
            }
            independent = any(
                authority
                for observations_for_mark in marked_observations.values()
                for _, _, authority in observations_for_mark
            )
            record = {
                "family": family,
                "quantity": (
                    quantity_name(next(iter(marked_observations)))
                    if len(marked_observations) == 1
                    else "conflict"
                ),
                "sources": sorted(source_names),
                "witnesses": sorted(
                    witness
                    for observations_for_mark in marked_observations.values()
                    for _, witness, _ in observations_for_mark
                ),
            }
            (independent_records if independent else derived_records).append(record)

        independent_conflict = any(
            record["quantity"] == "conflict" for record in independent_records
        )
        independent_quantities = {
            record["quantity"]
            for record in independent_records
            if record["quantity"] != "conflict"
        }
        if independent_conflict or len(independent_quantities) > 1:
            decision = "conflict"
        elif len(independent_records) >= 2:
            decision = "consensus_2_of_3"
        elif independent_records:
            decision = "single_source"
        else:
            decision = "derived_only"

        derived_quantities = {
            record["quantity"]
            for record in derived_records
            if record["quantity"] != "conflict"
        }
        derived_conflict = any(
            record["quantity"] == "conflict" for record in derived_records
        ) or bool(independent_quantities and derived_quantities - independent_quantities)
        decision_counts[decision] = decision_counts.get(decision, 0) + 1
        base = bases[(entry, slot)]
        positions.append(
            {
                "target": {
                    "kind": "stem",
                    "dictionary_entry": entry,
                    "slot": slot,
                },
                "base": base,
                "position": position,
                "letter": base[position],
                "decision": decision,
                "independent_support": len(independent_records),
                "independent_votes": independent_records,
                "derived_votes": derived_records,
                "derived_conflict": derived_conflict,
            }
        )

    return {
        "policy": "unambiguous-target-independent-family-per-position-v1",
        "position_indexing": "zero_based_logical_letters",
        "counts": {
            "positions": len(positions),
            "existing_confirmed_vote_positions": len(existing_votes),
            "excluded_ambiguous_candidate_positions": excluded_ambiguous_positions,
            **{
                decision: decision_counts.get(decision, 0)
                for decision in (
                    "consensus_2_of_3",
                    "single_source",
                    "conflict",
                    "derived_only",
                )
            },
        },
        "positions": positions,
    }


def render_report(
    candidates: tuple[Candidate, ...],
    existing_votes: tuple[ExistingQuantityVote, ...] = (),
) -> str:
    ambiguous = sum(candidate.alternatives > 1 for candidate in candidates)
    ambiguous_witnesses = len(
        {candidate.witness_key() for candidate in candidates if candidate.alternatives > 1}
    )
    conflicts = quantity_conflict_targets(candidates)
    conflict_candidates = sum(candidate.target_key() in conflicts for candidate in candidates)
    homographs = meaning_distinguishing_homographs(candidates)
    direct_homographs = meaning_distinguishing_homographs(
        candidate for candidate in candidates if candidate.alternatives == 1
    )
    homograph_targets = {
        target
        for targets in homographs.values()
        for target in targets
    }
    direct_homograph_targets = {
        target
        for targets in direct_homographs.values()
        for target in targets
    }
    source_summaries = []
    for source_name in sorted({item.dictionary.source_name for item in candidates}):
        source_candidates = tuple(
            item for item in candidates if item.dictionary.source_name == source_name
        )
        family, independent = SOURCE_FAMILIES[source_name]
        source_summaries.append(
            {
                "source": SOURCE_EVIDENCE_IDS[source_name],
                "family": family,
                "independent_quantity_authority": independent,
                "candidates": len(source_candidates),
                "witnesses": len(
                    {item.witness_key() for item in source_candidates}
                ),
                "targets": len({item.target_key() for item in source_candidates}),
            }
        )
    consensus = quantity_consensus(candidates, existing_votes)
    document = {
        "schema": "whitakers-words.quantity-candidates.v1",
        "policy": "review_only",
        "counts": {
            "candidates": len(candidates),
            "witnesses": len({candidate.witness_key() for candidate in candidates}),
            "targets": len({candidate.target_key() for candidate in candidates}),
            "ambiguous_candidates": ambiguous,
            "ambiguous_witnesses": ambiguous_witnesses,
            "unambiguous_candidates": len(candidates) - ambiguous,
            "quantity_conflict_targets": len(conflicts),
            "quantity_conflict_candidates": conflict_candidates,
            "meaning_distinguishing_homograph_groups": len(homographs),
            "meaning_distinguishing_homograph_targets": len(homograph_targets),
            "meaning_distinguishing_homograph_candidates": sum(
                candidate.target_key() in homographs.get(candidate.homograph_key(), {})
                for candidate in candidates
            ),
            "direct_homograph_groups": len(direct_homographs),
            "direct_homograph_targets": len(direct_homograph_targets),
            "independent_authority_candidates": sum(
                item.dictionary.independent_quantity_authority
                for item in candidates
            ),
            "derived_authority_candidates": sum(
                not item.dictionary.independent_quantity_authority
                for item in candidates
            ),
        },
        "sources": source_summaries,
        "consensus": consensus,
        "candidates": [],
    }
    for candidate in candidates:
        record = candidate.report_record(
            quantity_conflict=candidate.target_key() in conflicts
        )
        positions = homographs.get(candidate.homograph_key(), {}).get(
            candidate.target_key(), frozenset()
        )
        record["ascii_citation"] = candidate.word.source
        record["meaning_distinguishing_homograph"] = bool(positions)
        record["opposed_quantity_positions"] = sorted(positions)
        direct = candidate.target_key() in direct_homographs.get(
            candidate.homograph_key(), {}
        )
        record["homograph_priority"] = (
            "direct" if direct else "semantic_review" if positions else "none"
        )
        document["candidates"].append(record)
    return json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dictionary", type=Path, help="Whitaker DICTFILE.GEN")
    parser.add_argument("superdb", type=Path, help="read-only unified dictionary index")
    parser.add_argument(
        "--source",
        action="append",
        choices=sorted(SQLITE_SOURCE_NAMES),
        dest="sources",
    )
    parser.add_argument(
        "--collatinus-data",
        type=Path,
        help="Collatinus bin/data directory; adds the derived base lexicon",
    )
    parser.add_argument(
        "--collatinus-extended",
        action="store_true",
        help="also read lem_ext.la (requires --collatinus-data)",
    )
    parser.add_argument(
        "--latin-german",
        type=Path,
        help="read-only token_latim_german.sqlite morphological lexicon",
    )
    parser.add_argument(
        "--existing-evidence",
        type=Path,
        help="omit already-recorded source/target pairs",
    )
    parser.add_argument("--output", type=Path, required=True, help="review-only JSONL candidates")
    parser.add_argument("--report", type=Path, help="optional detailed JSON report")
    parser.add_argument("--maximum-ending", type=int, default=6)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.maximum_ending < 0 or arguments.maximum_ending > STEM_SIZE:
        raise SuggestionError("--maximum-ending must be in 0..18")
    if arguments.collatinus_extended and arguments.collatinus_data is None:
        raise SuggestionError("--collatinus-extended requires --collatinus-data")
    sources = arguments.sources or sorted(SQLITE_SOURCE_NAMES)
    stems = read_whitaker_stems(arguments.dictionary)
    entries: Iterable[DictionaryEntry] = read_dictionary_entries(
        arguments.superdb, sources
    )
    if arguments.collatinus_data is not None:
        entries = itertools.chain(
            entries,
            read_collatinus_entries(
                arguments.collatinus_data,
                include_extended=arguments.collatinus_extended,
            ),
        )
    if arguments.latin_german is not None:
        entries = itertools.chain(entries, read_latin_german_entries(arguments.latin_german))
    candidates = suggest(stems, entries, maximum_ending=arguments.maximum_ending)
    votes = existing_consensus_votes(arguments.existing_evidence)
    candidates = remove_existing(candidates, existing_keys(arguments.existing_evidence))
    arguments.output.write_text(render_jsonl(candidates), encoding="utf-8")
    if arguments.report is not None:
        arguments.report.write_text(render_report(candidates, votes), encoding="utf-8")
    print(f"wrote {len(candidates)} review-only candidates to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
