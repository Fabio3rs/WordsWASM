#!/usr/bin/env python3

"""Render the legacy Ada trick tables as typed REWRITES.LAT records."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


MAIN_TABLES = tuple(f"{letter}_Tricks" for letter in "ADEFGHKLMNOPSTUYZ")
EARLY_TABLES = tuple(f"{letter}_Slur_Tricks" for letter in "ACINOQS")


@dataclass(frozen=True)
class AdaTrick:
    operation: str
    first: str
    second: str = ""


def table(source: str, name: str) -> list[AdaTrick]:
    declaration = re.search(
        rf"\b{re.escape(name)}\s*:\s*constant\s+TricksT\s*:=\s*\((.*?)\n\s*\);",
        source,
        re.DOTALL,
    )
    if declaration is None:
        raise ValueError(f"Ada trick table not found: {name}")
    records = re.findall(
        r"\(Max\s*=>\s*\d+\s*,\s*Op\s*=>\s*(TC_\w+)\s*,\s*"
        r"(?:FF1|FF3|I1|S1)\s*=>\s*\+\"([^\"]*)\""
        r"(?:\s*,\s*(?:FF2|FF4|I2)\s*=>\s*\+\"([^\"]*)\")?\s*\)",
        declaration.group(1),
        re.DOTALL,
    )
    return [AdaTrick(*record) for record in records]


def stable_fragment(value: str) -> str:
    return value if value else "empty"


def literal_records(
    trick: AdaTrick,
    *,
    stage: str,
    scope: str,
    priority: int,
    era: str,
    meaning: str,
    required_initial: str | None = None,
) -> list[tuple[str, str]]:
    directions = [(trick.first, trick.second)]
    if trick.operation == "TC_Flip_Flop":
        directions.append((trick.second, trick.first))
    elif trick.operation not in {"TC_Flip", "TC_Internal"}:
        raise ValueError(f"not a literal Ada trick: {trick.operation}")

    output: list[tuple[str, str]] = []
    for before, after in directions:
        # Get_Tricks_Table dispatches by the input's first letter. This removes
        # the historically unreachable c->k entry from K_Tricks.
        if required_initial is not None and not before.startswith(required_initial):
            continue
        minimum_after = 2 if scope == "INITIAL" else 0
        name = (
            f"orth-{scope.lower()}-{stable_fragment(before)}-"
            f"{stable_fragment(after)}"
        )
        header = (
            f"ORTHOGRAPHIC {priority} {stage} LITERAL {scope} FORWARD "
            f"{before or '-'} {after or '-'} X 0 0 {minimum_after} ANY "
            f"{era} {name}"
        )
        output.append((header, meaning))
    return output


def render(root: Path) -> str:
    body = (root / "src/words_engine/words_engine-trick_tables.adb").read_text()
    spec = (root / "src/words_engine/words_engine-trick_tables.ads").read_text()
    records: list[tuple[str, str]] = []

    for name in EARLY_TABLES:
        initial = name[0].lower()
        for trick in table(body, name):
            if trick.operation == "TC_Slur":
                records.append((
                    "ORTHOGRAPHIC 0 EARLY SLUR INITIAL FORWARD "
                    f"{trick.first} - X 0 0 0 ANY CLASSICAL "
                    f"orth-slur-{trick.first}",
                    "A prefix-final consonant may assimilate to the following consonant",
                ))
            else:
                records.extend(literal_records(
                    trick,
                    stage="EARLY",
                    scope="INITIAL",
                    priority=0,
                    era="CLASSICAL",
                    meaning="Alternative assimilated prefix spelling",
                    required_initial=initial,
                ))

    records.append((
        "ORTHOGRAPHIC 0 FALLBACK LITERAL INITIAL FORWARD is iis V 0 0 0 "
        "EO_VERB CLASSICAL orth-eo-is-iis",
        "Some forms of eo insert i where its stem meets an is- sequence",
    ))

    for name in MAIN_TABLES:
        initial = name[0].lower()
        for trick in table(body, name):
            records.extend(literal_records(
                trick,
                stage="FALLBACK",
                scope="INITIAL",
                priority=0,
                era="CLASSICAL",
                meaning="Alternative initial spelling",
                required_initial=initial,
            ))

    for trick in table(spec, "Any_Tricks"):
        records.extend(literal_records(
            trick,
            stage="FALLBACK",
            scope="INTERNAL",
            priority=1,
            era="CLASSICAL",
            meaning="Alternative internal spelling",
        ))

    records.append((
        "ORTHOGRAPHIC 2 FALLBACK LITERAL FINAL FORWARD is iis ADJ 0 2 0 "
        "ADJECTIVE_IIS CLASSICAL orth-adjective-terminal-iis",
        "A terminal iis on a first-declension adjective may drop one i",
    ))

    for trick in table(spec, "Mediaeval_Tricks"):
        records.extend(literal_records(
            trick,
            stage="FALLBACK",
            scope="INTERNAL",
            priority=3,
            era="MEDIEVAL",
            meaning="Medieval spelling may use this substitution",
        ))

    records.append((
        "ORTHOGRAPHIC 4 FALLBACK DOUBLE_CONSONANT INTERNAL FORWARD - - X 0 0 0 "
        "ANY MEDIEVAL orth-medieval-double-consonant",
        "A doubled consonant may be rendered by a single consonant in Medieval spelling",
    ))

    if len(records) != 159:
        raise ValueError(f"expected 159 orthographic records, got {len(records)}")
    return "\n".join(f"{header}\n{meaning}" for header, meaning in records) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--check", type=Path)
    args = parser.parse_args()
    rendered = render(args.root)
    if args.check is None:
        print(rendered, end="")
        return

    source = args.check.read_text()
    begin_marker = "-- BEGIN IMPORTED ADA ORTHOGRAPHY\n"
    end_marker = "-- END IMPORTED ADA ORTHOGRAPHY\n"
    begin = source.index(begin_marker) + len(begin_marker)
    end = source.index(end_marker, begin)
    if source[begin:end] != rendered:
        raise ValueError("REWRITES.LAT differs from the Ada table import")


if __name__ == "__main__":
    main()
