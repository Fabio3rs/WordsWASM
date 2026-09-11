#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


DATASET_ID = "sha256:" + "0" * 64
HEADER_MINOR_OFFSET = 10
HEADER_SIZE = 40
HEADER_SECTION_COUNT_OFFSET = 16
HEADER_CRC32_OFFSET = 32
DIRECTORY_ENTRY_SIZE = 32
DIRECTORY_TYPE_OFFSET = 0
DIRECTORY_PAYLOAD_OFFSET = 8
DIRECTORY_BYTES_OFFSET = 16
DIRECTORY_COUNT_OFFSET = 24
DIRECTORY_STRIDE_OFFSET = 28
STEM_REFERENCES_SECTION = 5
LEGACY_MINOR = 9
PERSISTED_STEM_INDEX_MINOR = 10


def pack(
    packer: Path,
    source: Path,
    output: Path,
    profile: str,
    option: str | None,
) -> bytes:
    command = [str(packer), str(source), str(output), profile]
    if option is not None:
        command.append(option)
    subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return output.read_bytes()


def batch(cli: Path, database: Path, words: list[str]) -> bytes:
    completed = subprocess.run(
        [
            str(cli),
            "--database",
            str(database),
            "--dataset-id",
            DATASET_ID,
            "--format",
            "search-v2",
            "--batch-json-lines",
        ],
        input="".join(f"{word}\n" for word in words).encode(),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return completed.stdout


def section_shape(
    image: bytearray, requested_type: int
) -> tuple[int, int, int]:
    section_count = struct.unpack_from("<I", image, HEADER_SECTION_COUNT_OFFSET)[0]
    for index in range(section_count):
        entry = HEADER_SIZE + index * DIRECTORY_ENTRY_SIZE
        section_type = struct.unpack_from(
            "<I", image, entry + DIRECTORY_TYPE_OFFSET
        )[0]
        if section_type != requested_type:
            continue
        payload = struct.unpack_from(
            "<Q", image, entry + DIRECTORY_PAYLOAD_OFFSET
        )[0]
        byte_size = struct.unpack_from(
            "<Q", image, entry + DIRECTORY_BYTES_OFFSET
        )[0]
        count = struct.unpack_from(
            "<I", image, entry + DIRECTORY_COUNT_OFFSET
        )[0]
        stride = struct.unpack_from(
            "<I", image, entry + DIRECTORY_STRIDE_OFFSET
        )[0]
        if byte_size != count * stride:
            raise AssertionError("fixed-record section has inconsistent shape")
        return payload, count, stride
    raise AssertionError(f"section {requested_type} is absent")


def update_crc(image: bytearray) -> None:
    section_count = struct.unpack_from(
        "<I", image, HEADER_SECTION_COUNT_OFFSET
    )[0]
    payload_begin = HEADER_SIZE + section_count * DIRECTORY_ENTRY_SIZE
    struct.pack_into(
        "<I",
        image,
        HEADER_CRC32_OFFSET,
        zlib.crc32(image[payload_begin:]),
    )


def corrupt_persisted_order(image: bytes) -> bytes:
    corrupted = bytearray(image)
    payload, count, stride = section_shape(
        corrupted, STEM_REFERENCES_SECTION
    )
    if count < 2:
        raise AssertionError("stem reference fixture is unexpectedly empty")
    first = bytes(corrupted[payload : payload + stride])
    last_offset = payload + (count - 1) * stride
    last = bytes(corrupted[last_offset : last_offset + stride])
    corrupted[payload : payload + stride] = last
    corrupted[last_offset : last_offset + stride] = first

    update_crc(corrupted)
    return bytes(corrupted)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--packer", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    arguments = parser.parse_args()

    source = arguments.root.resolve() / "whitakers-words"
    words = sorted(
        set(
            re.findall(
                r"[A-Za-z]+",
                (source / "test/01_aeneid/input.txt").read_text().lower(),
            )
        )
    )
    if len(words) != 2726:
        raise AssertionError(f"unexpected Aeneid vocabulary size: {len(words)}")

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        legacy_path = directory / "legacy.wwdb"
        production_path = directory / "production.wwdb"
        repeated_path = directory / "production-repeat.wwdb"
        legacy_search_path = directory / "legacy-search.wwdb"
        production_search_path = directory / "production-search.wwdb"

        legacy = pack(
            arguments.packer,
            source,
            legacy_path,
            "dense",
            "--legacy-stem-order",
        )
        production = pack(
            arguments.packer,
            source,
            production_path,
            "dense",
            None,
        )
        repeated = pack(
            arguments.packer,
            source,
            repeated_path,
            "dense",
            None,
        )
        legacy_search = pack(
            arguments.packer,
            source,
            legacy_search_path,
            "search-only",
            "--legacy-stem-order",
        )
        production_search = pack(
            arguments.packer,
            source,
            production_search_path,
            "search-only",
            None,
        )

        if len(legacy) != len(production):
            raise AssertionError("persisted stem index changed the WWDB size")
        if production != repeated:
            raise AssertionError("persisted stem index output is nondeterministic")
        if (
            struct.unpack_from("<H", production, HEADER_MINOR_OFFSET)[0]
            != PERSISTED_STEM_INDEX_MINOR
        ):
            raise AssertionError("production packer did not select WWDB 1.10")
        if struct.unpack_from("<H", legacy, HEADER_MINOR_OFFSET)[0] != LEGACY_MINOR:
            raise AssertionError("legacy option did not select WWDB 1.9")
        if legacy == production:
            raise AssertionError("A/B databases unexpectedly have identical bytes")
        if len(legacy_search) != len(production_search):
            raise AssertionError("persisted index changed search-only WWDB size")
        if (
            struct.unpack_from("<H", production_search, HEADER_MINOR_OFFSET)[0]
            != PERSISTED_STEM_INDEX_MINOR
        ):
            raise AssertionError("production search packer did not select WWDB 1.10")

        legacy_output = batch(arguments.cli, legacy_path, words)
        production_output = batch(arguments.cli, production_path, words)
        if legacy_output != production_output:
            raise AssertionError("persisted stem index changed corpus results")
        legacy_search_output = batch(
            arguments.cli, legacy_search_path, words
        )
        production_search_output = batch(
            arguments.cli, production_search_path, words
        )
        if legacy_search_output != production_search_output:
            raise AssertionError(
                "persisted stem index changed search-only corpus results"
            )
        if production_output != production_search_output:
            raise AssertionError("full and search-only profiles disagree")

        corrupt_path = directory / "corrupt.wwdb"
        corrupt_path.write_bytes(corrupt_persisted_order(production))
        rejected = subprocess.run(
            [
                str(arguments.cli),
                "--database",
                str(corrupt_path),
                "--dataset-id",
                DATASET_ID,
                "--format",
                "search-v2",
                "amo",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if rejected.returncode != 3 or "invalid-index-order" not in rejected.stderr:
            raise AssertionError(
                "loader accepted a corrupt persisted stem index: "
                f"returncode={rejected.returncode}, stderr={rejected.stderr!r}"
            )

        print(
            "production persisted stem index: "
            f"{len(words)} corpus forms byte-identical in both profiles; "
            f"dense/search bytes={len(production)}/{len(production_search)}; "
            "legacy 1.9 compatible; corrupt production order rejected"
        )


if __name__ == "__main__":
    main()
