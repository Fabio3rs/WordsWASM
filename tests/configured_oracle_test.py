#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path


DATASET_ID = "sha256:" + "0" * 64

MODE_FLAGS = (
    "TRIM_OUTPUT", "HAVE_OUTPUT_FILE", "WRITE_OUTPUT_TO_FILE",
    "DO_UNKNOWNS_ONLY", "WRITE_UNKNOWNS_TO_FILE",
    "IGNORE_UNKNOWN_NAMES", "IGNORE_UNKNOWN_CAPS", "DO_COMPOUNDS",
    "DO_FIXES", "DO_TRICKS", "DO_DICTIONARY_FORMS", "SHOW_AGE",
    "SHOW_FREQUENCY", "DO_EXAMPLES", "DO_ONLY_MEANINGS",
    "DO_STEMS_FOR_UNKNOWN",
)

MDEV_FLAGS = (
    "HAVE_STATISTICS_FILE", "WRITE_STATISTICS_FILE", "SHOW_DICTIONARY",
    "SHOW_DICTIONARY_LINE", "SHOW_DICTIONARY_CODES", "DO_PEARSE_CODES",
    "DO_ONLY_INITIAL_WORD", "FOR_WORD_LIST_CHECK", "DO_ONLY_FIXES",
    "DO_FIXES_ANYWAY", "USE_PREFIXES", "USE_SUFFIXES", "USE_TACKONS",
    "DO_MEDIEVAL_TRICKS", "DO_SYNCOPE", "DO_TWO_WORDS",
    "INCLUDE_UNKNOWN_CONTEXT", "NO_MEANINGS", "OMIT_ARCHAIC",
    "OMIT_MEDIEVAL", "OMIT_UNCOMMON", "DO_I_FOR_J", "DO_U_FOR_V",
    "PAUSE_IN_SCREEN_OUTPUT", "NO_SCREEN_ACTIVITY",
    "UPDATE_LOCAL_DICTIONARY", "UPDATE_MEANINGS", "MINIMIZE_OUTPUT",
)

BASE_MODE = {
    name: False for name in MODE_FLAGS
} | {
    "TRIM_OUTPUT": True,
    "IGNORE_UNKNOWN_NAMES": True,
    "IGNORE_UNKNOWN_CAPS": True,
    "DO_COMPOUNDS": True,
    "DO_FIXES": True,
    "DO_TRICKS": True,
    "DO_DICTIONARY_FORMS": True,
}

BASE_MDEV = {
    name: False for name in MDEV_FLAGS
} | {
    "DO_ONLY_INITIAL_WORD": True,
    "USE_PREFIXES": True,
    "USE_SUFFIXES": True,
    "USE_TACKONS": True,
    "DO_MEDIEVAL_TRICKS": True,
    "DO_SYNCOPE": True,
    "NO_SCREEN_ACTIVITY": True,
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_boolean_file(path: Path, order: tuple[str, ...], values: dict) -> None:
    path.write_text("".join(
        f"{name:<34}{'Y' if values[name] else 'N'}\n" for name in order
    ))


def write_configuration(directory: Path, mode_overrides=None,
                        mdev_overrides=None) -> None:
    mode = BASE_MODE | (mode_overrides or {})
    mdev = BASE_MDEV | (mdev_overrides or {})
    if set(mode) != set(MODE_FLAGS) or set(mdev) != set(MDEV_FLAGS):
        raise AssertionError("configured oracle fixture must be complete")
    write_boolean_file(directory / "WORD.MOD", MODE_FLAGS, mode)
    write_boolean_file(directory / "WORD.MDV", MDEV_FLAGS, mdev)
    with (directory / "WORD.MDV").open("a") as output:
        output.write(f"{'START_FILE_CHARACTER':<33}'@'\n")
        output.write(f"{'CHANGE_PARAMETERS_CHARACTER':<33}'#'\n")
        output.write(f"{'CHANGE_DEVELOPER_MODES_CHARACTER':<33}'!'\n")


def run_lines(command: list[str], queries: tuple[str, ...], *, cwd: Path,
              env=None) -> dict[str, dict]:
    completed = subprocess.run(
        [*command, "--batch-json-lines"], cwd=cwd, env=env, check=True,
        input="".join(f"{query}\n" for query in queries), text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    documents = [json.loads(line) for line in completed.stdout.splitlines()]
    if len(documents) != len(queries):
        raise AssertionError(
            f"oracle emitted {len(documents)} documents for {len(queries)} "
            f"queries; stderr={completed.stderr!r}")
    return dict(zip(queries, documents, strict=True))


def signatures(document: dict) -> list[tuple]:
    return sorted((
        item["lexeme"]["entryId"],
        item["partOfSpeech"],
        json.dumps(item["morphology"], sort_keys=True, separators=(",", ":")),
    ) for item in document["analyses"])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cpp", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    ada_root = root / "whitakers-words"
    source_mdv = ada_root / "WORD.MDV"
    source_mdv_was_present = source_mdv.exists()
    original_mdv_hash = sha256(source_mdv) if source_mdv_was_present else None
    database = ada_root / "poc/compact-db/output/words-poc-dense.wwdb"
    native_base = [
        str(args.cpp.resolve()), "--database", str(database),
        "--dataset-id", DATASET_ID, "--format", "analysis-v2",
    ]

    def compare_profile(queries: tuple[str, ...], *, mode=None, mdev=None,
                        native_flags=()) -> dict[str, dict]:
        with tempfile.TemporaryDirectory(prefix="words-configured-oracle-") as raw:
            data = Path(raw)
            for source in ada_root.iterdir():
                if source.is_file() and source.name not in {"WORD.MOD", "WORD.MDV"}:
                    (data / source.name).symlink_to(source)
            write_configuration(data, mode, mdev)
            environment = os.environ.copy()
            environment["WHITAKERS_WORDS_DATADIR"] = str(data)
            ada = run_lines(
                [str((ada_root / "bin/words_json").resolve()), "--configured"],
                queries, cwd=data, env=environment)
            native = run_lines(
                [*native_base, *native_flags], queries, cwd=root)
            for query in queries:
                if signatures(ada[query]) != signatures(native[query]):
                    raise AssertionError(
                        f"configured morphology differs for {query}: "
                        f"Ada={signatures(ada[query])!r}, "
                        f"C++={signatures(native[query])!r}")
            return ada

    trim_queries = ("reg", "dic", "liceo", "audetur", "audemur", "ausi")
    untrimmed = compare_profile(
        trim_queries, mode={"TRIM_OUTPUT": False})
    if untrimmed["audetur"]["status"] != "analyzed":
        raise AssertionError("untrimmed Ada witness did not expose audetur")

    compare_profile(
        ("archipuella",), mdev={"USE_PREFIXES": False},
        native_flags=("--no-prefixes",))
    compare_profile(
        ("anaticulus",), mdev={"USE_SUFFIXES": False},
        native_flags=("--no-suffixes",))
    compare_profile(
        ("puellaque", "quispiam", "ecquidam"),
        mdev={"USE_TACKONS": False},
        native_flags=("--no-tackons", "--no-packons", "--no-tickons"))
    compare_profile(
        ("amasti",), mdev={"DO_SYNCOPE": False},
        native_flags=("--no-syncope",))
    compare_profile(
        ("archipuella", "anaticulus"), mode={"DO_FIXES": False},
        native_flags=("--no-fixes",))
    medieval_off = compare_profile(
        ("teologia", "pretor"), mdev={"DO_MEDIEVAL_TRICKS": False},
        native_flags=("--orthography=classical",))
    if medieval_off["teologia"]["status"] != "unknown" or \
            medieval_off["pretor"]["status"] != "analyzed":
        raise AssertionError("medieval/classical oracle witnesses are invalid")
    compare_profile(
        ("pretor",), mode={"DO_TRICKS": False},
        native_flags=("--orthography=disabled",))

    if source_mdv.exists() != source_mdv_was_present:
        raise AssertionError(
            "configured oracle changed whether source WORD.MDV exists")
    if original_mdv_hash is not None and \
            sha256(source_mdv) != original_mdv_hash:
        raise AssertionError("configured oracle modified the source WORD.MDV")


if __name__ == "__main__":
    main()
