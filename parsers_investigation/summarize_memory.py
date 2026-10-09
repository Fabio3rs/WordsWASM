"""Summarize local profiles and compare outputs, ignoring only elapsedNs."""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics


def normalized(value):
    if isinstance(value, dict):
        return {key: normalized(item) for key, item in value.items() if key != "elapsedNs"}
    if isinstance(value, list):
        return [normalized(item) for item in value]
    return value


def read_output(path):
    content = path.read_text()
    try:
        values = [json.loads(content)]
    except json.JSONDecodeError:
        values = [json.loads(line) for line in content.splitlines() if line.strip()]
    return normalized(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for directory in args.directories:
        for row in json.loads((directory / "summary.json").read_text()):
            rows.append({**row, "directory": str(directory)})
    native = defaultdict(list)
    references = {}
    for row in rows:
        if row["tool"] == "native":
            native[row["case"]].append(row)
            references.setdefault(row["case"], Path(row["directory"]) / f'{row["case"]}.native.{row["repetition"]}.stdout')
    verification = []
    for case, reference in references.items():
        expected = read_output(reference)
        for row in rows:
            if row["case"] != case:
                continue
            path = Path(row["directory"]) / f'{case}.{row["tool"]}.{row["repetition"]}.stdout'
            equal = expected == read_output(path)
            verification.append({"output": str(path), "reference": str(reference), "equalExceptElapsedNs": equal})
            if not equal:
                raise ValueError(f"Output changed: {path}")
    # Inclusive stack matches: groups overlap and must NOT be added together.
    patterns = {
        "database-load": "Database::load_poc",
        "read-database": "read_database",
        "tree-materialization": "populate_tree_best",
        "morphology-materialization": "populate_best",
        "manual-candidate-scoring": "candidate_score",
        "beam": "enumerate_beam",
        "synthetic-relinearizations": "synthetic_relinearizations",
        "sequential-model": "markov_model.hpp:",
        "dependency-header-including-extractor": "dependency_markov.hpp:",
        "dependency-factor-extraction": "extract_factors",
        "json-output": "parsers::to_json",
    }
    profiles = {}
    for row in rows:
        if row["tool"] != "dhat":
            continue
        path = Path(row["directory"]) / f'{row["case"]}.dhat.1.profile'
        data = json.loads(path.read_text())
        def frames(point):
            return [data["ftbl"][index] for index in point["fs"]]
        groups = {}
        for name, pattern in patterns.items():
            points = [p for p in data["pps"] if any(pattern in f for f in frames(p))]
            groups[name] = {key: sum(p[key] for p in points) for key in ["tb", "tbk", "gb", "gbk"]}
        top = {}
        for key in ["tb", "gb"]:
            top[key] = [{"allocatedBytes": p["tb"], "allocatedBlocks": p["tbk"],
                         "globalPeakLiveBytes": p["gb"], "frames": frames(p)}
                        for p in sorted(data["pps"], key=lambda p: p[key], reverse=True)[:12]]
        profiles[row["case"]] = {"inclusiveStackMatches": groups, "topSites": top}
    table = []
    for case, samples in native.items():
        dhat = next((r for r in rows if r["case"] == case and r["tool"] == "dhat"), {})
        table.append({
            "case": case,
            "nativeMedianSeconds": statistics.median(r["elapsedSeconds"] for r in samples),
            "nativeMedianRssKiB": statistics.median(r["maxRssKiB"] for r in samples),
            "nativeRssRangeKiB": [min(r["maxRssKiB"] for r in samples), max(r["maxRssKiB"] for r in samples)],
            "stdoutBytes": samples[0]["stdoutBytes"],
            **{key: dhat.get(key) for key in ["allocatedBytes", "allocatedBlocks", "peakLiveBytes", "atEndBytes"]},
        })
    args.output.write_text(json.dumps({"table": table, "verification": verification,
                                       "dhat": profiles}, indent=2) + "\n")
    print(f"Verified {len(verification)} outputs; wrote {args.output}")
    for row in table:
        print(json.dumps(row))


if __name__ == "__main__":
    main()
