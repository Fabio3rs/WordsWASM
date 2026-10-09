"""Compare complete outputs of pre/post sharing binaries, without tolerances."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from profile_memory import LONG_TEXT, ROOT


def read_records(path):
    text = path.read_text()
    try:
        return [json.loads(text)]
    except json.JSONDecodeError:
        return [json.loads(line) for line in text.splitlines() if line.strip()]


def canonical(records):
    # Only top-level execution metadata may differ; compare every nested field.
    normalized = [{k: v for k, v in record.items()
                   if k not in ("elapsedNs", "sourceCommit")} for record in records]
    return json.dumps(normalized, sort_keys=True, separators=(",", ":"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-build", type=Path, required=True)
    parser.add_argument("--after-build", type=Path, required=True)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    database = args.database.resolve()
    common = ["--database", str(database), "--dataset-id",
              "sha256:" + hashlib.sha256(database.read_bytes()).hexdigest()]
    corpus = ["--corpus", str(ROOT / "parsers_investigation/corpus/agreement_fixtures.json")]
    cases = {}
    for strategy in ("dependency-tree-oracle", "dependency-mst", "dependency-eisner"):
        cases[strategy] = ("parsers_investigation", [*corpus, "--strategy", strategy, "--include-nbest"])
        cases["compound-" + strategy] = ("parsers_investigation", ["--text", "amatus est",
            "--strategy", strategy, "--json", "--include-nbest"])
    cases["fragment"] = ("parsers_investigation", ["--text", "Bona puella", "--fragment",
        "--strategy", "dependency-tree-oracle", "--json", "--include-nbest"])
    cases["long-beam-diagnostics"] = ("parsers_investigation", ["--text", LONG_TEXT,
        "--strategy", "dependency-mst", "--json", "--include-nbest", "--include-rejections"])
    cases["long-eisner"] = ("parsers_investigation", ["--text", LONG_TEXT,
        "--strategy", "dependency-eisner", "--json", "--include-nbest"])
    cases["markov-additive"] = ("markov_parser_ranker", [*corpus, "--evaluation-tier", "attested"])
    cases["markov-backoff"] = ("markov_parser_ranker", [*corpus, "--evaluation-tier", "attested",
        "--smoothing", "hierarchical-backoff"])
    cases["dependency-markov"] = ("dependency_markov_ranker", corpus)
    results = []
    for case, (program, arguments) in cases.items():
        row = {"case": case, "ignoredTopLevelFields": ["elapsedNs", "sourceCommit"]}
        outputs = []
        for label, build in [("before", args.before_build), ("after", args.after_build)]:
            binary = (build / "parsers_investigation" / program).resolve()
            command = [str(binary), *common, *arguments]
            path = output / f"{case}.{label}.json"
            with path.open("w") as stdout:
                subprocess.run(command, cwd=ROOT, stdout=stdout, check=True, timeout=120)
            records = read_records(path)
            outputs.append(canonical(records))
            row[label] = {"command": command,
                          "binarySha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                          "outputSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                          "records": len(records),
                          "sourceCommits": sorted({r.get("sourceCommit", "") for r in records})}
        row["equal"] = outputs[0] == outputs[1]
        results.append(row)
        (output / "verification.json").write_text(json.dumps(results, indent=2) + "\n")
        if not row["equal"]:
            raise RuntimeError(f"Result changed: {case}")
        print(f"Identical: {case}", flush=True)


if __name__ == "__main__":
    main()
