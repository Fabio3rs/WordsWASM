#!/usr/bin/env python3

"""Compare LatinaeTabulae.ods claims with INFLECTS.SEC quantities.

This audit is intentionally conservative.  It maps the generic noun table
directly, records every remaining vowel-bearing RuleId, and explains why the
other sheets need an additional morphology/segmentation step before their
marks can be promoted.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import sys
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable


FORMAT = "words.latinae-quantity-audit-1"
INFLECTION_STRIDE = 40
VOWELS = frozenset("aeiouy")
DIPHTHONGS = frozenset({"ae", "au", "ei", "eu", "oe", "ui"})
MACRON = "\N{COMBINING MACRON}"
BREVE = "\N{COMBINING BREVE}"
PARTS = {
    1: "noun",
    2: "pronoun",
    3: "packon",
    4: "adjective",
    5: "numeral",
    6: "adverb",
    7: "verb",
    8: "participle",
    9: "supine",
    10: "preposition",
    11: "conjunction",
    12: "interjection",
}
CASES = {
    "Nominativus": (1,),
    "Vocativus": (2,),
    "Genetivus": (3,),
    "Dativus": (5,),
    "Ablativus": (6,),
    "Accusativus": (7,),
    "Nom/Voc": (1, 2),
    "Dat/Abl": (5, 6),
}

# Bibliographic citation: Benjamin L. D'Ooge, Latin for Beginners,
# Ginn and Company, 1909/1911; Project Gutenberg eBook 18251.  This edition
# predates ISBN.  URL: https://www.gutenberg.org/ebooks/18251
DOOGE_CITATION = {
    "id": "dooge-latin-for-beginners-18251",
    "author": "Benjamin L. D'Ooge",
    "title": "Latin for Beginners",
    "edition": "Ginn and Company, 1909/1911; Project Gutenberg eBook 18251",
    "isbn": None,
    "url": "https://www.gutenberg.org/ebooks/18251",
}

# Bibliographic citation: Napoleão Mendes de Almeida, Gramática latina: curso
# único e completo, 29. ed., 5. tir., Saraiva, 2000/2005.
# ISBN-10: 85-02-00307-0; ISBN-13: 978-85-02-00307-1.
# URL: https://latim.paginas.ufsc.br/files/2012/06/Gram%C3%A1tica-Latina-Napole%C3%A3o-Mendes-de-Almeida.pdf
ALMEIDA_CITATION = {
    "id": "almeida-gramatica-latina-29",
    "author": "Napoleão Mendes de Almeida",
    "title": "Gramática latina: curso único e completo",
    "edition": "29. ed., 5. tir., São Paulo: Saraiva, 2000/2005",
    "isbn10": "85-02-00307-0",
    "isbn13": "978-85-02-00307-1",
    "url": (
        "https://latim.paginas.ufsc.br/files/2012/06/"
        "Gram%C3%A1tica-Latina-Napole%C3%A3o-Mendes-de-Almeida.pdf"
    ),
}


class AuditError(ValueError):
    """A source invariant required by the audit was violated."""


def _load_extractor(script: Path):
    specification = importlib.util.spec_from_file_location(
        "latinae_tabulae_extractor", script
    )
    if specification is None or specification.loader is None:
        raise AuditError("cannot load LatinaeTabulae extractor")
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


def _uint(data: bytes, offset: int, width: int) -> int:
    return int.from_bytes(data[offset:offset + width], "little")


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_rules(path: Path) -> list[dict[str, Any]]:
    source = path.read_bytes()
    if len(source) % INFLECTION_STRIDE:
        raise AuditError("INFLECTS.SEC has a partial record")
    rules: list[dict[str, Any]] = []
    for record_index in range(len(source) // INFLECTION_STRIDE):
        begin = record_index * INFLECTION_STRIDE
        record = source[begin:begin + INFLECTION_STRIDE]
        if record[0] == 0:
            continue
        ending_size = _uint(record, 24, 4)
        ending = record[28:28 + ending_size].decode("ascii")
        rules.append(
            {
                "ruleId": len(rules),
                "recordIndex": record_index,
                "partOfSpeechCode": record[0],
                "partOfSpeech": PARTS.get(record[0], f"unknown-{record[0]}"),
                "which": _uint(record, 4, 4),
                "variant": _uint(record, 8, 4),
                "case": record[12],
                "number": record[13],
                "gender": record[14],
                "tense": record[12],
                "voice": record[13],
                "mood": record[14],
                "person": record[15],
                "verbNumber": record[16],
                "participleCase": record[15],
                "participleNumber": record[16],
                "participleGender": record[17],
                "stemKey": _uint(record, 20, 4),
                "ending": ending,
            }
        )
    return rules


def load_inflection_quantities(path: Path) -> dict[int, tuple[int, int]]:
    result: dict[int, tuple[int, int]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        clean = line.strip()
        if not clean or clean.startswith("--") or not clean.startswith("INFLECTION "):
            continue
        fields = clean.split()
        if len(fields) != 4:
            raise AuditError(f"{path}:{line_number}: malformed INFLECTION row")
        rule_id, known, long_vowel = map(int, fields[1:])
        if rule_id in result:
            raise AuditError(f"{path}:{line_number}: duplicate RuleId {rule_id}")
        result[rule_id] = (known, long_vowel)
    return result


def _letters_and_explicit(text: str) -> tuple[str, dict[int, str]]:
    letters: list[str] = []
    marks: dict[int, str] = {}
    for character in unicodedata.normalize("NFD", text):
        if unicodedata.combining(character) == 0:
            if "a" <= character.lower() <= "z":
                letters.append(character.lower())
            continue
        if character in {MACRON, BREVE} and letters:
            marks[len(letters) - 1] = "long" if character == MACRON else "short"
    return "".join(letters), marks


def _nominal_alternative(text: str) -> dict[str, Any]:
    base, explicit = _letters_and_explicit(text)
    quantities: dict[int, str] = dict(explicit)
    origins: dict[int, str] = {position: "explicit" for position in explicit}
    for position, letter in enumerate(base):
        if letter not in VOWELS or position in quantities:
            continue
        left = base[position - 1:position + 1] if position else ""
        right = base[position:position + 2]
        if left in DIPHTHONGS or right in DIPHTHONGS:
            continue
        quantities[position] = "short"
        origins[position] = "inferred-short"
    return {
        "source": text,
        "base": base,
        "quantities": [
            {"position": position, "quantity": quantities[position], "origin": origins[position]}
            for position in sorted(quantities)
        ],
    }


def _common_alternative_claim(alternatives: Iterable[dict[str, Any]]) -> dict[str, Any]:
    items = list(alternatives)
    base = items[0]["base"]
    if any(item["base"] != base for item in items):
        raise AuditError("alternatives must have the same ASCII base")
    mappings = [
        {quantity["position"]: (quantity["quantity"], quantity["origin"])
         for quantity in item["quantities"]}
        for item in items
    ]
    common_positions = set(mappings[0])
    for mapping in mappings[1:]:
        common_positions &= set(mapping)
    claims = []
    for position in sorted(common_positions):
        values = {mapping[position][0] for mapping in mappings}
        if len(values) != 1:
            continue
        origins = {mapping[position][1] for mapping in mappings}
        claims.append(
            {
                "position": position,
                "quantity": values.pop(),
                "origin": "explicit" if origins == {"explicit"} else "inferred-short",
            }
        )
    return {"base": base, "quantities": claims}


def _masks(claim: dict[str, Any]) -> tuple[int, int]:
    known = 0
    long_vowel = 0
    for quantity in claim["quantities"]:
        bit = 1 << quantity["position"]
        known |= bit
        if quantity["quantity"] == "long":
            long_vowel |= bit
    return known, long_vowel


def _marked(base: str, known: int, long_vowel: int) -> str:
    output = []
    for position, character in enumerate(base):
        output.append(character)
        bit = 1 << position
        if known & bit:
            output.append(MACRON if long_vowel & bit else BREVE)
    return unicodedata.normalize("NFC", "".join(output))


def declension_claims(workbook: dict[str, Any]) -> list[dict[str, Any]]:
    try:
        sheet = next(sheet for sheet in workbook["sheets"] if sheet["name"] == "Declinationes")
    except StopIteration as error:
        raise AuditError("workbook has no Declinationes sheet") from error
    cells = {(cell["row"], cell["column"]): cell for cell in sheet["cells"]}
    claims: list[dict[str, Any]] = []
    number = 0
    headers: dict[int, int] = {}
    for row in range(1, sheet["extent"]["rows"] + 1):
        first = cells.get((row, 1))
        if first and first["text"] == "Singular":
            number = 1
            continue
        if first and first["text"] == "Plural":
            number = 2
            continue
        if first and first["text"] == "Casus":
            headers = {}
            for column in range(2, 10):
                header = cells.get((row, column))
                if header:
                    match = re.match(r"([1-5])", header["text"])
                    if match:
                        headers[column] = int(match.group(1))
            continue
        if not first or first["text"] not in CASES or not number:
            continue

        for column in range(2, 10):
            source = cells.get((row, column))
            if source is None:
                continue
            for covered_column in range(column, column + source["columnSpan"]):
                declension = headers.get(covered_column)
                if declension is None:
                    raise AuditError(f"missing declension header above {source['ref']}")
                raw_alternatives = [
                    alternative.strip()
                    for alternative in source["text"].replace("–", "-").split("/")
                    if alternative.strip()
                ]
                grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
                for alternative in raw_alternatives:
                    parsed = _nominal_alternative(alternative)
                    grouped[parsed["base"]].append(parsed)
                for base, alternatives in grouped.items():
                    common = _common_alternative_claim(alternatives)
                    known, long_vowel = _masks(common)
                    claims.append(
                        {
                            "sheet": "Declinationes",
                            "cell": source["ref"],
                            "sourceText": source["text"],
                            "declension": declension,
                            "cases": list(CASES[first["text"]]),
                            "number": number,
                            "ending": base,
                            "known": known,
                            "long": long_vowel,
                            "marked": _marked(base, known, long_vowel),
                            "quantities": common["quantities"],
                        }
                    )
    return claims


def pronominal_forms(workbook: dict[str, Any]) -> list[dict[str, Any]]:
    try:
        sheet = next(sheet for sheet in workbook["sheets"] if sheet["name"] == "Pronomina")
    except StopIteration as error:
        raise AuditError("workbook has no Pronomina sheet") from error
    cells = {(cell["row"], cell["column"]): cell for cell in sheet["cells"]}
    forms: list[dict[str, Any]] = []

    # (heading row, first case row, last case row, first paradigm column,
    #  engine POS, engine inflection class, engine variant when significant)
    blocks = (
        (1, 4, 8, 2, "pronoun", (3,), None),
        (1, 4, 8, 8, "pronoun", (6, 4), None),
        (1, 4, 8, 14, "pronoun", (4,), None),
        (10, 13, 17, 2, "pronoun", (1,), None),
        (10, 13, 17, 8, "adjective", (1,), None),
        (10, 13, 17, 14, "pronoun", (6, 4), None),
    )
    for heading_row, first_row, last_row, first_column, part, whiches, variant in blocks:
        heading = cells[(heading_row, first_column)]
        for row in range(first_row, last_row + 1):
            case_cell = cells[(row, 1)]
            cases = CASES.get(case_cell["text"])
            if cases is None or len(cases) != 1:
                raise AuditError(f"unsupported pronominal case at {case_cell['ref']}")
            for source_column in range(first_column, first_column + 6):
                source = cells.get((row, source_column))
                if source is None:
                    continue
                for column in range(source_column, source_column + source["columnSpan"]):
                    offset = column - first_column
                    number = 1 if offset < 3 else 2
                    gender = (1, 3, 2)[offset % 3]
                    for alternative in source["text"].split("/"):
                        base, explicit = _letters_and_explicit(alternative.strip())
                        forms.append(
                            {
                                "sheet": "Pronomina",
                                "cell": source["ref"],
                                "sourceText": source["text"],
                                "paradigm": heading["text"],
                                "partOfSpeech": part,
                                "whiches": whiches,
                                "variant": variant,
                                "case": cases[0],
                                "number": number,
                                "gender": gender,
                                "form": base,
                                "explicit": explicit,
                            }
                        )

    personal_heading = cells[(19, 2)]
    personal_columns = {
        2: (5, 1, 1),  # ego/me
        3: (5, 3, 2),  # nos
        4: (5, 2, 1),  # tu/te
        5: (5, 3, 2),  # vos
        6: (5, 4, 0),  # se/sui are number-neutral in INFLECTS.SEC
    }
    for row in range(20, 25):
        case_cell = cells[(row, 1)]
        cases = CASES.get(case_cell["text"])
        if cases is None or len(cases) != 1:
            raise AuditError(f"unsupported personal-pronoun case at {case_cell['ref']}")
        for column, (which, variant, number) in personal_columns.items():
            source = cells.get((row, column))
            if source is None:
                continue
            base, explicit = _letters_and_explicit(source["text"])
            forms.append(
                {
                    "sheet": "Pronomina",
                    "cell": source["ref"],
                    "sourceText": source["text"],
                    "paradigm": personal_heading["text"],
                    "partOfSpeech": "pronoun",
                    "whiches": (which,),
                    "variant": variant,
                    "case": cases[0],
                    "number": number,
                    "gender": 4,
                    "form": base,
                    "explicit": explicit,
                }
            )
    return forms


def project_pronominal_forms(
    forms: Iterable[dict[str, Any]], rules: list[dict[str, Any]]
) -> tuple[dict[int, dict[str, Any]], list[dict[str, Any]]]:
    projected: dict[int, dict[str, Any]] = {}
    lexical: list[dict[str, Any]] = []
    for form in forms:
        if not form["explicit"]:
            continue
        candidates = [
            rule for rule in rules
            if rule["partOfSpeech"] == form["partOfSpeech"]
            and rule["which"] in form["whiches"]
            and (form["variant"] is None or rule["variant"] == form["variant"])
            and rule["case"] == form["case"]
            and rule["number"] == form["number"]
            and rule["gender"] in {0, form["gender"]}
            and rule["ending"]
            and form["form"].endswith(rule["ending"])
        ]
        if not candidates:
            lexical.append(form)
            continue
        longest = max(len(rule["ending"]) for rule in candidates)
        candidates = [rule for rule in candidates if len(rule["ending"]) == longest]
        for rule in candidates:
            offset = len(form["form"]) - len(rule["ending"])
            known = 0
            long_vowel = 0
            for position, quantity in form["explicit"].items():
                if position < offset:
                    continue
                bit = 1 << (position - offset)
                known |= bit
                if quantity == "long":
                    long_vowel |= bit
            if not known:
                lexical.append(form)
                continue
            previous = projected.get(rule["ruleId"])
            if previous is None:
                projected[rule["ruleId"]] = {
                    "known": known,
                    "long": long_vowel,
                    "sources": [form],
                }
                continue
            conflict = previous["known"] & known & (previous["long"] ^ long_vowel)
            if conflict:
                raise AuditError(f"pronominal source conflict for RuleId {rule['ruleId']}")
            previous["long"] = (previous["long"] & ~known) | long_vowel
            previous["known"] |= known
            previous["sources"].append(form)
    return projected, lexical


def _rule_summary(rule: dict[str, Any]) -> dict[str, Any]:
    summary = {
        key: rule[key]
        for key in (
            "ruleId", "partOfSpeech", "which", "variant", "stemKey", "ending"
        )
    }
    if rule["partOfSpeech"] in {"noun", "pronoun", "adjective", "numeral", "supine"}:
        summary.update(
            {"case": rule["case"], "number": rule["number"], "gender": rule["gender"]}
        )
    elif rule["partOfSpeech"] == "participle":
        summary.update(
            {
                "tense": rule["tense"],
                "voice": rule["voice"],
                "case": rule["participleCase"],
                "number": rule["participleNumber"],
                "gender": rule["participleGender"],
            }
        )
    elif rule["partOfSpeech"] == "verb":
        summary.update(
            {
                "tense": rule["tense"], "voice": rule["voice"], "mood": rule["mood"],
                "person": rule["person"], "number": rule["verbNumber"],
            }
        )
    return summary


def compile_audit(root: Path, study_root: Path | None = None) -> dict[str, Any]:
    extractor = _load_extractor(
        root / "whitakers-words/poc/compact-db/extract_latinae_tabulae.py"
    )
    workbook = extractor.extract_workbook(root / "LatinaeTabulae.ods")
    rules = load_rules(root / "whitakers-words/INFLECTS.SEC")
    annotated = load_inflection_quantities(root / "whitakers-words/QUANTITIES.LAT")
    claims = declension_claims(workbook)
    pronoun_forms = pronominal_forms(workbook)
    pronoun_projection, lexical_pronoun_forms = project_pronominal_forms(
        pronoun_forms, rules
    )
    dooge = None if study_root is None else study_root / "pg18251-images.html"
    almeida = None if study_root is None else study_root / (
        "gramatica-latina-luna-completa/publicacao-preview/"
        "gramatica-latina-napoleao-mendes-de-almeida.html"
    )
    if study_root is not None and (
        dooge is None or almeida is None or not dooge.is_file() or not almeida.is_file()
    ):
        raise AuditError("local comparison grammars are missing")
    comparison_grammars = []
    workbook_issues = []
    if dooge is not None and almeida is not None:
        comparison_grammars = [
            {
                **DOOGE_CITATION,
                "path": ".study/pg18251-images.html",
                "sha256": _sha256(dooge),
                "role": "independent-paradigm-confirmation",
            },
            {
                **ALMEIDA_CITATION,
                "path": (
                    ".study/gramatica-latina-luna-completa/publicacao-preview/"
                    "gramatica-latina-napoleao-mendes-de-almeida.html"
                ),
                "sha256": _sha256(almeida),
                "role": "ocr-comparison-not-automatic-promotion",
            },
        ]
        workbook_issues = _comparison_workbook_issues()

    direct: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for claim in claims:
        for rule in rules:
            if (
                rule["partOfSpeech"] == "noun"
                and rule["which"] == claim["declension"]
                and rule["case"] in claim["cases"]
                and rule["number"] == claim["number"]
                and rule["ending"] == claim["ending"]
            ):
                direct[rule["ruleId"]].append(claim)

    comparisons = []
    for rule_id in sorted(direct):
        candidates = direct[rule_id]
        quantities = {(claim["known"], claim["long"]) for claim in candidates}
        if len(quantities) != 1:
            status = "source-conflict"
            expected = None
        else:
            expected = quantities.pop()
            actual = annotated.get(rule_id)
            if expected[0] == 0:
                status = "source-has-no-per-letter-quantity"
            elif actual is None:
                status = "missing"
            elif actual == expected:
                status = "exact"
            elif actual[0] & expected[0] == actual[0] and actual[1] == (expected[1] & actual[0]):
                status = "partial"
            else:
                status = "conflict"
        comparisons.append(
            {
                "rule": _rule_summary(rules[rule_id]),
                "status": status,
                "expected": None
                if expected is None
                else {"known": expected[0], "long": expected[1], "marked": _marked(rules[rule_id]["ending"], *expected)},
                "actual": None
                if rule_id not in annotated
                else {"known": annotated[rule_id][0], "long": annotated[rule_id][1], "marked": _marked(rules[rule_id]["ending"], *annotated[rule_id])},
                "sources": [
                    {
                        "sheet": claim["sheet"], "cell": claim["cell"],
                        "text": claim["sourceText"], "quantities": claim["quantities"],
                    }
                    for claim in candidates
                ],
            }
        )

    by_part: dict[str, dict[str, Any]] = {}
    for part in PARTS.values():
        part_rules = [rule for rule in rules if rule["partOfSpeech"] == part]
        vowel_rules = [rule for rule in part_rules if any(vowel in rule["ending"] for vowel in VOWELS)]
        known_rules = [rule for rule in vowel_rules if rule["ruleId"] in annotated]
        missing_rules = [rule for rule in vowel_rules if rule["ruleId"] not in annotated]
        by_part[part] = {
            "rules": len(part_rules),
            "vowelBearingRules": len(vowel_rules),
            "annotatedRules": len(known_rules),
            "missingRules": len(missing_rules),
            "missingRuleIds": [rule["ruleId"] for rule in missing_rules],
        }

    reasons = {
        "noun": {
            "directButDiphthongOnly": [
                item["rule"]["ruleId"] for item in comparisons
                if item["status"] == "source-has-no-per-letter-quantity"
            ],
            "notRepresentedBySimplifiedTable": [
                rule["ruleId"] for rule in rules
                if rule["partOfSpeech"] == "noun"
                and any(vowel in rule["ending"] for vowel in VOWELS)
                and rule["ruleId"] not in direct
                and rule["ruleId"] not in annotated
            ],
            "why": "Greek, archaic, variant, locative, and omitted i-stem forms are outside the simplified Declinationes grid; ae is a diphthong and has no independent per-letter quantity mask.",
        },
        "pronoun": {
            "why": "Pronomina gives marked surface forms, not their engine stem/ending split; marks must be projected only after matching each paradigm lexeme to compatible rules.",
        },
        "adjective": {
            "why": "Nominal case endings can be reused only after confirming adjective-class and combined degree+case suffix boundaries; nullus is stored as ADJ although it appears on Pronomina.",
        },
        "numeral": {
            "why": "Many numeral endings combine numeral formation with case endings, and the workbook contains no general numeral paradigm.",
        },
        "verb": {
            "why": "Verba separates personal endings, conjugation vowels, and tense/mood markers, while INFLECTS.SEC stores many of them combined; irregular and historical variants are not covered by the grid.",
        },
        "participle": {
            "why": "Most participial rule strings combine tense/voice material with adjectival case endings; only the already-reviewed -us rule was directly projected.",
        },
        "supine": {
            "why": "The ODS has no supine row. The two rules require the grammar witnesses -um and -u rather than an inference from noun declensions alone.",
        },
    }

    statuses = Counter(item["status"] for item in comparisons)
    pronoun_comparisons = []
    for rule_id, projection in sorted(pronoun_projection.items()):
        actual = annotated.get(rule_id)
        expected = (projection["known"], projection["long"])
        if actual is None:
            status = "missing"
        elif actual == expected:
            status = "exact"
        elif actual[0] & expected[0] == actual[0] and actual[1] == (expected[1] & actual[0]):
            status = "partial"
        else:
            status = "conflict"
        pronoun_comparisons.append(
            {
                "rule": _rule_summary(rules[rule_id]),
                "status": status,
                "expected": {
                    "known": expected[0],
                    "long": expected[1],
                    "marked": _marked(rules[rule_id]["ending"], *expected),
                },
                "actual": None
                if actual is None
                else {
                    "known": actual[0],
                    "long": actual[1],
                    "marked": _marked(rules[rule_id]["ending"], *actual),
                },
                "sources": [
                    {
                        "sheet": source["sheet"],
                        "cell": source["cell"],
                        "text": source["sourceText"],
                        "paradigm": source["paradigm"],
                        "form": source["form"],
                    }
                    for source in projection["sources"]
                ],
            }
        )
    pronoun_statuses = Counter(item["status"] for item in pronoun_comparisons)
    return {
        "format": FORMAT,
        "sources": {
            "workbook": workbook["source"],
            "inflections": "whitakers-words/INFLECTS.SEC",
            "quantities": "whitakers-words/QUANTITIES.LAT",
            "comparisonGrammars": comparison_grammars,
        },
        "workbook": {
            "sheetCount": workbook["sheetCount"],
            "sheets": [
                {
                    "name": sheet["name"],
                    "extent": sheet["extent"],
                    "nonEmptyCellCount": sheet["nonEmptyCellCount"],
                    "explicitQuantityMarkCount": sum(
                        len(cell["explicitQuantities"]) for cell in sheet["cells"]
                    ),
                }
                for sheet in workbook["sheets"]
            ],
        },
        "counts": {
            "rules": len(rules),
            "annotatedInflectionRules": len(annotated),
            "vowelBearingRules": sum(
                1 for rule in rules if any(vowel in rule["ending"] for vowel in VOWELS)
            ),
            "unannotatedVowelBearingRules": sum(
                1 for rule in rules
                if any(vowel in rule["ending"] for vowel in VOWELS)
                and rule["ruleId"] not in annotated
            ),
            "directDeclensionComparisons": len(comparisons),
            "directDeclensionStatuses": dict(sorted(statuses.items())),
            "explicitPronominalComparisons": len(pronoun_comparisons),
            "explicitPronominalStatuses": dict(sorted(pronoun_statuses.items())),
            "explicitPronominalFormsInLexicalStem": len(
                {
                    (form["cell"], form["form"])
                    for form in lexical_pronoun_forms
                    if form["explicit"]
                }
            ),
        },
        "byPartOfSpeech": by_part,
        "directDeclensionComparisons": comparisons,
        "explicitPronominalComparisons": pronoun_comparisons,
        "explicitPronominalLexicalForms": [
            {
                "sheet": form["sheet"],
                "cell": form["cell"],
                "text": form["sourceText"],
                "paradigm": form["paradigm"],
                "form": form["form"],
            }
            for form in lexical_pronoun_forms
            if form["explicit"]
        ],
        "workbookIssues": workbook_issues,
        "remainingReasons": reasons,
    }


def _comparison_workbook_issues() -> list[dict[str, Any]]:
    """Locally reviewed findings; emitted only with the optional .study corpus.

    Locators deliberately use printed sections and searchable expressions.
    Source-code line numbers are unstable OCR implementation details and are
    therefore not citations.
    """

    return [
            {
                "kind": "incorrect-form",
                "sheet": "Pronomina",
                "cell": "P16",
                "observed": "ipsā",
                "expected": "ipsī",
                "reason": "The dative singular of ipse is ipsī in all three genders.",
                "confirmation": [
                    {
                        "sourceId": DOOGE_CITATION["id"],
                        "locator": "§ 481, DEMONSTRATIVE, paradigm ipse; row ‘Dat.’",
                        "expression": "Dat. ipsī ipsī ipsī",
                    },
                    {
                        "sourceId": ALMEIDA_CITATION["id"],
                        "locator": "§ 208, Ipse, ipsa, ipsum, printed p. 163; row ‘DAT.’",
                        "expression": "DAT. ipsi ipsi ipsi",
                    },
                ],
            },
            {
                "kind": "missing-form",
                "sheet": "Pronomina",
                "cell": "G13",
                "observed": "",
                "expected": "quae",
                "reason": "The feminine nominative plural of qui is quae.",
                "confirmation": [{
                    "sourceId": DOOGE_CITATION["id"],
                    "locator": "§ 221, relative pronoun quī, quae, quod; row ‘Nom.’",
                    "expression": "Plural: quī quae quae",
                }],
            },
            {
                "kind": "missing-form",
                "sheet": "Pronomina",
                "cell": "I14",
                "observed": "",
                "expected": "nullum",
                "reason": "The neuter accusative singular of nullus is nullum.",
                "confirmation": [{
                    "sourceId": DOOGE_CITATION["id"],
                    "locator": "§ 109, PARADIGMS, nūllus; row ‘Acc.’",
                    "expression": "Acc. nūllum nūllam nūllum",
                }],
            },
            {
                "kind": "missing-form",
                "sheet": "Pronomina",
                "cell": "O14",
                "observed": "",
                "expected": "ipsum",
                "reason": "The neuter accusative singular of ipse is ipsum.",
                "confirmation": [{
                    "sourceId": DOOGE_CITATION["id"],
                    "locator": "§ 481, DEMONSTRATIVE, paradigm ipse; row ‘Acc.’",
                    "expression": "Acc. ipsum ipsam ipsum",
                }],
            },
            {
                "kind": "missing-quantity-mark",
                "sheet": "Pronomina",
                "cell": "Q4",
                "observed": "eī / ii",
                "expected": "eī / iī",
                "reason": "The contracted spelling iī represents the same long ī as eī.",
                "confirmation": [{
                    "sourceId": DOOGE_CITATION["id"],
                    "locator": "§ 114, paradigm is, ea, id; nominative plural",
                    "expression": "eī (or iī)",
                }],
            },
            {
                "kind": "missing-quantity-mark",
                "sheet": "Pronomina",
                "cell": "Q7",
                "observed": "eīs / iis",
                "expected": "eīs / iīs",
                "reason": "The alternative iīs has long ī, like eīs.",
                "confirmation": [{
                    "sourceId": DOOGE_CITATION["id"],
                    "locator": "§ 114, paradigm is, ea, id; dative plural",
                    "expression": "eīs (or iīs)",
                }],
            },
            {
                "kind": "missing-quantity-mark",
                "sheet": "Pronomina",
                "cell": "Q8",
                "observed": "eīs / iis",
                "expected": "eīs / iīs",
                "reason": "The alternative iīs has long ī, like eīs.",
                "confirmation": [{
                    "sourceId": DOOGE_CITATION["id"],
                    "locator": "§ 114, paradigm is, ea, id; ablative plural",
                    "expression": "eīs (or iīs)",
                }],
            },
    ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument(
        "--study-root",
        type=Path,
        help="optional untracked directory containing the two comparison grammars",
    )
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    study_root = None if arguments.study_root is None else arguments.study_root.resolve()
    document = compile_audit(arguments.root.resolve(), study_root)
    rendered = json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if arguments.output is None:
        print(rendered, end="")
    else:
        arguments.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
