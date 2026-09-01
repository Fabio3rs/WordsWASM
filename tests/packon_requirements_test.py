#!/usr/bin/env python3
"""Audit the typed PACKON ledger against the legacy DICTLINE convention."""

import argparse
import re
from pathlib import Path


LEGACY_PACKON = re.compile(
    r"\sPACK\s+\d+\s+\d+\s+\S+.*?\(w/-([a-z]+)\)"
)


def read_ledger(path: Path) -> dict[int, str]:
    result: dict[int, str] = {}
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        clean = line.strip()
        if not clean or clean.startswith("--"):
            continue
        fields = clean.split()
        if len(fields) != 2 or not fields[0].isdigit() or not fields[1].isalpha():
            raise AssertionError(f"invalid ledger line {line_number}: {line!r}")
        entry = int(fields[0])
        if entry in result:
            raise AssertionError(f"duplicate ledger entry {entry}")
        result[entry] = fields[1]
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()

    data_root = args.root / "whitakers-words"
    legacy: dict[int, str] = {}
    for entry, line in enumerate(
        (data_root / "DICTLINE.GEN").read_text().splitlines(), 1
    ):
        match = LEGACY_PACKON.search(line)
        if match:
            legacy[entry] = match.group(1)

    ledger = read_ledger(data_root / "PACKON_REQUIREMENTS.LAT")
    if ledger != legacy:
        missing = sorted(legacy.keys() - ledger.keys())
        extra = sorted(ledger.keys() - legacy.keys())
        mismatched = sorted(
            entry
            for entry in legacy.keys() & ledger.keys()
            if legacy[entry] != ledger[entry]
        )
        raise AssertionError(
            f"PACKON ledger drift: missing={missing}, extra={extra}, "
            f"mismatched={mismatched}"
        )

    packer = (
        data_root / "poc/compact-db/wwdb_poc_pack.cpp"
    ).read_text()
    if "meaning.starts_with" in packer or '"PACKON w/"' in packer:
        raise AssertionError("the packer must not classify grammar from meanings")

    print(f"audited {len(ledger)} typed PACKON requirements")


if __name__ == "__main__":
    main()
