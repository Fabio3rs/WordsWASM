"""Local native/DHAT/Callgrind baseline; does not configure builds or CI."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time


ROOT = Path(__file__).resolve().parent.parent
LONG_TEXT = (
    "Quae res in Civitate duae plurimum possunt, eae contra nos ambae "
    "faciunt in hoc tempore, summa gratia et eloquentia;"
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(*command: str) -> str:
    return subprocess.check_output(command, cwd=ROOT, text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/parsers-memory")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--tools", nargs="+", choices=["native", "dhat", "callgrind"],
                        default=["native", "dhat"])
    parser.add_argument("--cases", nargs="+")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    if args.repeats < 1 or args.timeout <= 0:
        parser.error("repeats and timeout must be positive")
    build = args.build.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary_dir = build / "parsers_investigation"
    database = args.database.resolve()
    corpus = ROOT / "parsers_investigation/corpus/agreement_fixtures.json"
    common = ["--database", str(database), "--dataset-id", "sha256:" + digest(database)]
    parser_bin = str(binary_dir / "parsers_investigation")
    seq_bin = str(binary_dir / "markov_parser_ranker")
    dep_bin = str(binary_dir / "dependency_markov_ranker")
    corpus_args = ["--corpus", str(corpus)]
    cases = {
        "morphology-corpus": [parser_bin, *common, *corpus_args, "--strategy", "morphology"],
        "mst-corpus": [parser_bin, *common, *corpus_args, "--strategy", "dependency-mst"],
        "oracle-corpus": [parser_bin, *common, *corpus_args, "--strategy", "dependency-tree-oracle"],
        "markov-additive": [seq_bin, *common, *corpus_args, "--evaluation-tier", "attested"],
        "markov-backoff": [seq_bin, *common, *corpus_args, "--evaluation-tier", "attested",
                           "--smoothing", "hierarchical-backoff"],
        "dependency-markov": [dep_bin, *common, *corpus_args],
        "long-morphology": [parser_bin, *common, "--text", LONG_TEXT, "--strategy", "morphology", "--json"],
        "long-beam": [parser_bin, *common, "--text", LONG_TEXT, "--strategy", "dependency-mst", "--json"],
        "long-beam-nbest": [parser_bin, *common, "--text", LONG_TEXT, "--strategy", "dependency-mst",
                            "--json", "--include-nbest"],
        "long-beam-rejections": [parser_bin, *common, "--text", LONG_TEXT, "--strategy", "dependency-mst",
                                 "--json", "--include-rejections"],
        "long-beam-diagnostics": [parser_bin, *common, "--text", LONG_TEXT, "--strategy", "dependency-mst",
                                  "--json", "--include-nbest", "--include-rejections"],
    }
    selected = args.cases or list(cases)
    if unknown := set(selected) - cases.keys():
        parser.error(f"unknown cases: {sorted(unknown)}")
    sources = sorted((ROOT / "parsers_investigation").glob("*.cpp"))
    sources += sorted((ROOT / "parsers_investigation").glob("*.hpp"))
    inputs = [database, *sorted((ROOT / "parsers_investigation/corpus").glob("*.json")),
              ROOT / "parsers_investigation/corpus/common_phrases.tsv", *sources,
              Path(__file__).resolve()]
    binaries = sorted({Path(cases[case][0]) for case in selected})
    metadata = {
        "createdUtc": datetime.now(timezone.utc).isoformat(),
        "commit": capture("git", "rev-parse", "HEAD"),
        "gitStatus": capture("git", "status", "--short"),
        "platform": platform.platform(),
        "valgrind": capture("valgrind", "--version"),
        "nativeTime": capture("/usr/bin/time", "--version"),
        "build": str(build),
        "cmakeCache": (build / "CMakeCache.txt").read_text(),
        "sha256": {str(path.relative_to(ROOT)): digest(path) for path in inputs},
        "binarySha256": {str(path): digest(path) for path in binaries},
        "commands": {case: cases[case] for case in selected},
        "tools": args.tools,
        "nativeRepeats": args.repeats,
        "timeoutSeconds": args.timeout,
        "notes": "Fresh processes, sequential runs, warm OS cache uncontrolled. Native RSS only; Valgrind wall time is not performance.",
    }
    (output / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n")
    results = []
    environment = dict(os.environ, LC_ALL="C")
    for tool in args.tools:
        for case in selected:
            for repetition in range(args.repeats if tool == "native" else 1):
                stem = output / f"{case}.{tool}.{repetition + 1}"
                command = cases[case]
                if tool == "native":
                    command = ["/usr/bin/time", "-f", '{"elapsedSeconds":%e,"userSeconds":%U,"systemSeconds":%S,"maxRssKiB":%M}',
                               "-o", str(stem) + ".time.json", *command]
                else:
                    command = ["valgrind", f"--tool={tool}", "--num-callers=32",
                               f"--log-file={stem}.log", f"--{tool}-out-file={stem}.profile", *command]
                print(f"Running {tool}: {case} ({repetition + 1})", flush=True)
                start = time.monotonic()
                with Path(str(stem) + ".stdout").open("wb") as stdout, Path(str(stem) + ".stderr").open("wb") as stderr:
                    completed = subprocess.run(command, cwd=ROOT, env=environment, stdout=stdout,
                                               stderr=stderr, timeout=args.timeout, check=True)
                row = {"case": case, "tool": tool, "repetition": repetition + 1,
                       "command": command, "exitCode": completed.returncode,
                       "instrumentedWallSeconds": time.monotonic() - start,
                       "stdoutBytes": Path(str(stem) + ".stdout").stat().st_size}
                if tool == "native":
                    row.update(json.loads(Path(str(stem) + ".time.json").read_text()))
                elif tool == "dhat":
                    profile = json.loads(Path(str(stem) + ".profile").read_text())
                    points = profile["pps"]
                    # gb/gbk are allocations live at the SAME global peak.
                    # Summing per-point mb would overstate the global peak.
                    row.update({
                        "allocatedBytes": sum(p["tb"] for p in points),
                        "allocatedBlocks": sum(p["tbk"] for p in points),
                        "peakLiveBytes": sum(p["gb"] for p in points),
                        "peakLiveBlocks": sum(p["gbk"] for p in points),
                        "atEndBytes": sum(p["eb"] for p in points),
                        "atEndBlocks": sum(p["ebk"] for p in points),
                    })
                else:
                    with Path(str(stem) + ".profile").open() as profile:
                        for line in profile:
                            if line.startswith(("events:", "summary:")):
                                key, value = line.split(":", 1)
                                row[key] = value.strip()
                results.append(row)
                (output / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
                print(json.dumps({k: v for k, v in row.items() if k != "command"}), flush=True)


if __name__ == "__main__":
    main()
