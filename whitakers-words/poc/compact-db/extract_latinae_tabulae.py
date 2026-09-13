#!/usr/bin/env python3

"""Extract the local Latin paradigm spreadsheet without LibreOffice.

The output is deliberately close to the ODS source: every non-empty cell has
an A1 reference, its merge span, its original text, and any explicit quantity
marks.  Semantic consumers can therefore be audited against the workbook
without treating a visual transcription as the source of truth.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import unicodedata
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from xml.etree import ElementTree


FORMAT = "words.latinae-tabulae-extract-1"
TABLE_NS = "urn:oasis:names:tc:opendocument:xmlns:table:1.0"
TEXT_NS = "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
OFFICE_NS = "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
NS = {"table": TABLE_NS, "text": TEXT_NS, "office": OFFICE_NS}
MACRON = "\N{COMBINING MACRON}"
BREVE = "\N{COMBINING BREVE}"


class ExtractionError(ValueError):
    """The ODS package is malformed or outside the supported subset."""


def _name(namespace: str, local: str) -> str:
    return f"{{{namespace}}}{local}"


def column_name(column: int) -> str:
    if column < 1:
        raise ValueError("column is one-based")
    result = ""
    while column:
        column, remainder = divmod(column - 1, 26)
        result = chr(ord("A") + remainder) + result
    return result


def cell_reference(row: int, column: int) -> str:
    return f"{column_name(column)}{row}"


def _positive_attribute(element: ElementTree.Element, attribute: str) -> int:
    value = element.get(attribute, "1")
    try:
        parsed = int(value)
    except ValueError as error:
        raise ExtractionError(f"invalid ODS repeat/span value {value!r}") from error
    if parsed < 1:
        raise ExtractionError(f"ODS repeat/span must be positive, got {parsed}")
    return parsed


def _cell_text(cell: ElementTree.Element) -> str:
    paragraphs = [
        "".join(paragraph.itertext())
        for paragraph in cell.findall("text:p", NS)
    ]
    return "\n".join(paragraphs).strip()


def quantity_observations(text: str) -> list[dict[str, Any]]:
    """Return only marks literally present in a cell.

    An unmarked vowel is intentionally absent here.  Whether it means a short
    paradigm vowel is a semantic inference made by the audit layer, not an ODS
    extraction fact.
    """

    observations: list[dict[str, Any]] = []
    logical_index = -1
    for character in unicodedata.normalize("NFD", text):
        if unicodedata.combining(character) == 0:
            logical_index += 1
            continue
        if character not in {MACRON, BREVE}:
            continue
        observations.append(
            {
                "index": logical_index,
                "quantity": "long" if character == MACRON else "short",
            }
        )
    return observations


@dataclass(frozen=True)
class ExtractedSheet:
    name: str
    cells: tuple[dict[str, Any], ...]

    def report(self) -> dict[str, Any]:
        max_row = max((cell["row"] + cell["rowSpan"] - 1 for cell in self.cells), default=0)
        max_column = max(
            (cell["column"] + cell["columnSpan"] - 1 for cell in self.cells),
            default=0,
        )
        return {
            "name": self.name,
            "extent": {"rows": max_row, "columns": max_column},
            "nonEmptyCellCount": len(self.cells),
            "cells": list(self.cells),
        }


def _extract_sheet(table: ElementTree.Element) -> ExtractedSheet:
    name = table.get(_name(TABLE_NS, "name"))
    if not name:
        raise ExtractionError("ODS table has no name")

    cells: list[dict[str, Any]] = []
    row_number = 1
    for row in table.findall("table:table-row", NS):
        row_repeat = _positive_attribute(row, _name(TABLE_NS, "number-rows-repeated"))
        column_number = 1
        row_cells: list[dict[str, Any]] = []
        for cell in row:
            if cell.tag not in {
                _name(TABLE_NS, "table-cell"),
                _name(TABLE_NS, "covered-table-cell"),
            }:
                continue
            repeat = _positive_attribute(cell, _name(TABLE_NS, "number-columns-repeated"))
            text = _cell_text(cell)
            if text:
                if repeat > 1_024:
                    raise ExtractionError("refusing to expand a repeated non-empty cell")
                for repeated in range(repeat):
                    column = column_number + repeated
                    row_cells.append(
                        {
                            "ref": cell_reference(row_number, column),
                            "row": row_number,
                            "column": column,
                            "rowSpan": _positive_attribute(
                                cell, _name(TABLE_NS, "number-rows-spanned")
                            ),
                            "columnSpan": _positive_attribute(
                                cell, _name(TABLE_NS, "number-columns-spanned")
                            ),
                            "text": text,
                            "explicitQuantities": quantity_observations(text),
                        }
                    )
            column_number += repeat

        if row_cells:
            if row_repeat > 1_024:
                raise ExtractionError("refusing to expand a repeated non-empty row")
            for repeated in range(row_repeat):
                for source in row_cells:
                    target = dict(source)
                    target["row"] = row_number + repeated
                    target["ref"] = cell_reference(target["row"], target["column"])
                    cells.append(target)
        row_number += row_repeat

    return ExtractedSheet(name, tuple(cells))


def extract_workbook(path: Path) -> dict[str, Any]:
    source = path.read_bytes()
    try:
        with zipfile.ZipFile(path) as package:
            mime = package.read("mimetype")
            content = package.read("content.xml")
    except (KeyError, zipfile.BadZipFile) as error:
        raise ExtractionError(f"invalid ODS package: {path}") from error
    if mime != b"application/vnd.oasis.opendocument.spreadsheet":
        raise ExtractionError("source is not an OpenDocument spreadsheet")

    try:
        root = ElementTree.fromstring(content)
    except ElementTree.ParseError as error:
        raise ExtractionError("invalid ODS content.xml") from error
    sheets = tuple(_extract_sheet(table) for table in root.findall(".//table:table", NS))
    if not sheets:
        raise ExtractionError("ODS workbook has no sheets")
    return {
        "format": FORMAT,
        "source": {
            "path": path.name,
            "sha256": hashlib.sha256(source).hexdigest(),
        },
        "sheetCount": len(sheets),
        "sheets": [sheet.report() for sheet in sheets],
    }


def render(workbook: dict[str, Any]) -> str:
    return json.dumps(workbook, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    output = render(extract_workbook(arguments.source))
    if arguments.output is None:
        print(output, end="")
    else:
        arguments.output.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
