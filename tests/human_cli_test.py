#!/usr/bin/env python3
"""Integration checks for the typed human CLI projection."""

import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile


CLI, DATABASE, SEARCH_DATABASE = sys.argv[1:4]


def run(*args, input_text=None):
    result = subprocess.run(
        [CLI, "--database", DATABASE, *args],
        input=input_text,
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, (args, result.stderr)
    return result.stdout


def rows(*args, input_text=None):
    output = run("--format", "human", "--human-style", "compact", *args,
                 input_text=input_text)
    lines = output.splitlines()
    header = lines[0].split("\t")
    assert header == [
        "result", "unit", "reading", "status", "input", "display",
        "quantity_coverage", "lemma", "part", "features", "meaning", "note",
    ]
    assert all(len(line.split("\t")) == len(header) for line in lines[1:])
    return [dict(zip(header, line.split("\t"))) for line in lines[1:]]


for word in ("amamus", "malum", "mālum", "puella", "exercitus",
             "hortor", "ausi", "res", "IV"):
    document = json.loads(run("--format", "analysis-v3", word))
    visible = rows(word)
    main = [row for row in visible if row["unit"] == "main" and row["reading"] != "0"]
    assert len(main) == len(document["analyses"]), word
    assert [(row["display"], row["quantity_coverage"]) for row in main] == [
        (item["form"]["display"], item["form"]["quantity"]["coverage"])
        for item in document["analyses"]
    ], word

assert rows("amamus")[0]["quantity_coverage"] == "none"
assert any(row["quantity_coverage"] == "partial" for row in rows("mālum"))
assert any(row["quantity_coverage"] == "complete" for row in rows("mālum"))
assert "quantity evidence none" in run("--format", "human", "amamus")
assert "display mālum · quantity evidence partial" in run(
    "--format", "human", "malum")
assert "display mālŭm · quantity evidence complete" in run(
    "--format", "human", "malum")
assert "display pŭellă · quantity evidence partial" in run(
    "--format", "human", "puella")
assert "display pŭellā · quantity evidence partial" in run(
    "--format", "human", "puella")
assert "display rēs · quantity evidence complete" in run(
    "--format", "human", "res")
assert "display exercĭtŭs · quantity evidence partial" in run(
    "--format", "human", "exercitus")
user_marked = run("--format", "human", "--detailed", "exērcitus")
assert "input exērcitus · display exercĭtŭs · quantity evidence partial" in user_marked
assert "Database-marked form: exercĭtŭs" in user_marked
sancte = rows("sanctē")
assert [row["display"] for row in sancte] == ["sancte", "sancte", "sanctē"]
assert [row["quantity_coverage"] for row in sancte] == [
    "none", "none", "partial",
]
marked_compound = rows("amātūrus est")
assert any(row["unit"] == "construction" and
           row["input"] == "amātūrus est" and
           row["display"] == "amaturus est" and
           row["quantity_coverage"] == "none"
           for row in marked_compound)

deponent = rows("hortor")
assert any("dep" in row["part"] for row in deponent)
assert all("passive" not in row["features"] for row in deponent if "dep" in row["part"])
semideponent = rows("ausi")
assert any("semidep" in row["part"] for row in semideponent)
assert all("passive" not in row["features"] for row in semideponent
           if "semidep" in row["part"])
semideponent_present = rows("audebantur")
assert any("semidep" in row["part"] for row in semideponent_present)
assert all("passive" not in row["features"] for row in semideponent_present
           if "semidep" in row["part"])
assert "Deponent verb:" in run("--format", "human", "--detailed", "hortor")
assert "Semideponent verb:" in run("--format", "human", "--detailed", "ausi")
assert "Lexical details:" in run("--format", "human", "--detailed", "audio")
assert "[res publica => the state]" not in run("--format", "human", "publica")
assert "[res publica => the state]" in run(
    "--format", "human", "--detailed", "publica")
assert any("[res publica => the state]" in row["meaning"] for row in
           rows("--detailed", "publica"))

compound = run("--format", "human", "--detailed", "amaturus est")
assert compound.startswith("Construction: amaturus est\n")
assert "active periphrastic with est" in compound
assert "future active participle" in compound.lower()
assert "be fond of" in compound
assert "PPL+" not in compound
assert "present indicative passive" not in compound
active_rows = rows("amaturus est")
assert [row["unit"] for row in active_rows].count("construction") == 1
assert [row["unit"] for row in active_rows].count("token:1") == 1
assert "active periphrastic" in active_rows[0]["features"]
assert active_rows[0]["part"] == "verb"
assert "active periphrastic with sum" in rows("amaturus sum")[0]["features"]

passive = run("--format", "human", "--detailed", "amandus est")
assert passive.startswith("Construction: amandus est\n")
assert "passive periphrastic with est" in passive
assert "gerundive combines" in passive.lower()
assert "present indicative passive" not in passive
assert "PPL+" not in passive
assert "passive periphrastic with est" in rows("amandus est")[0]["features"]

army_rows = rows("exercitus est")
assert [row["unit"] for row in army_rows].count("construction") == 1
assert [row["unit"] for row in army_rows].count("token:1") == 7
assert [row["unit"] for row in army_rows].count("token:2") == 2
assert army_rows[0]["input"] == "exercitus est"
assert all(row["input"] == "exercitus" for row in army_rows
           if row["unit"] == "token:1")
assert "perfect indicative passive" in army_rows[0]["features"]
assert len(json.loads(run("--format", "analysis-v3", "exercitus est"))["analyses"]) == 8

before = rows("res")
after = rows("--filter-trim=deponent-active-form", "res")
assert len(after) < len(before)
assert all(row["part"] == "noun" for row in after)
empty = rows("--filter-trim=unsupported-short-imperative", "reg")
assert empty[0]["reading"] == "0"
assert empty[0]["status"] == "analyzed"
assert "hidden" in empty[0]["note"]
unknown = rows("xyzxyz")
assert unknown[0]["reading"] == "0"
assert unknown[0]["status"] == "unknown"
split = rows("--two-words=legacy", "respublica")
assert split[0]["status"] == "unknown"
assert {row["unit"] for row in split[1:]} == {"suggestion:1", "suggestion:2"}
assert all(row["status"] == "suggested" for row in split[1:])
enclitic_split = run("--format", "human", "--two-words=legacy",
                     "respublicaque")
assert "possible split: res + publica + -que" in enclitic_split
assert "Possible split, part 2: publica + -que" in enclitic_split
assert "Form: input publicaque · display publica" in enclitic_split
enclitic_rows = rows("--two-words=legacy", "respublicaque")
assert "res + publica + -que" in enclitic_rows[0]["note"]
assert all("enclitic -que" in row["note"] for row in enclitic_rows
           if row["unit"] == "suggestion:2")
assert all("enclitic -que" not in row["note"] for row in enclitic_rows
           if row["unit"] == "suggestion:1")

stream = rows("--input", "-", input_text="amo\npuella\n")
assert {row["result"] for row in stream} == {"1", "2"}

assert "\x1b[" not in run("--format", "human", "amo")
assert "\x1b[" not in run("--format", "human", "--color=never", "amo")
assert "\x1b[" in run("--format", "human", "--color=always", "amo")
assert "\x1b[" not in run("--format", "human", "--human-style=compact", "amo")

if os.name == "posix":
    import pty

    master, slave = pty.openpty()
    try:
        process = subprocess.Popen(
            [CLI, "--database", DATABASE, "--format", "human", "malum"],
            stdout=slave, stderr=subprocess.PIPE,
        )
        os.close(slave)
        slave = -1
        output = bytearray()
        while True:
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            if not chunk:
                break
            output.extend(chunk)
        assert process.wait() == 0, process.stderr.read()
        assert b"\x1b[" in output
        assert "mālŭm".encode("utf8") in output
    finally:
        os.close(master)
        if slave != -1:
            os.close(slave)

for args in (
    ("--format", "human", "--pretty", "amo"),
    ("--format", "human", "--human-style=compact", "--color=always", "amo"),
    ("--format", "analysis-v3", "--detailed", "amo"),
    ("--format", "analysis-v3", "--color=auto", "amo"),
    ("--format", "analysis-v3", "--human-style=normal", "amo"),
):
    result = subprocess.run(
        [CLI, "--database", DATABASE, *args], capture_output=True, text=True,
        encoding="utf-8")
    assert result.returncode == 2, (args, result.stderr)

search_only = subprocess.run(
    [CLI, "--database", SEARCH_DATABASE, "--format", "human", "amo"],
    capture_output=True, text=True, encoding="utf-8")
assert search_only.returncode == 3
assert "requires a full WWDB" in search_only.stderr

if sys.platform == "linux" and platform.machine() == "x86_64" and shutil.which("node"):
    wrapper = Path(__file__).resolve().parent.parent / "npm/wordswasm-cli/bin/wordswasm.cjs"
    with tempfile.TemporaryDirectory(prefix="wordswasm-human-wrapper-") as temp:
        platform_root = (Path(temp) / "node_modules" / "@fabiors" /
                         "wordswasm-cli-linux-x64")
        (platform_root / "bin").mkdir(parents=True)
        (platform_root / "data").mkdir()
        (platform_root / "package.json").write_text(
            '{"name":"@fabiors/wordswasm-cli-linux-x64","version":"0.0.0"}',
            encoding="utf8")
        (platform_root / "bin" / "words_cli").symlink_to(Path(CLI).resolve())
        (platform_root / "data" / "words-full.wwdb").symlink_to(
            Path(DATABASE).resolve())
        environment = dict(os.environ, NODE_PATH=str(Path(temp) / "node_modules"))
        def wrapper_run(*args, input_text=None):
            result = subprocess.run(
                ["node", str(wrapper), *args], input=input_text, text=True,
                encoding="utf-8", capture_output=True, env=environment)
            assert result.returncode == 0, (args, result.stderr)
            return result.stdout

        assert "quantity evidence" in wrapper_run("malum")
        assert json.loads(wrapper_run("--format", "analysis-v3", "amo"))[
            "schemaVersion"] == 3
        assert json.loads(wrapper_run("--format", "analysis-v4", "amo"))[
            "schemaVersion"] == 4
        assert json.loads(wrapper_run("--pretty", "amo"))["schemaVersion"] == 4
        batch = [json.loads(line) for line in wrapper_run(
            "--batch-json-lines", input_text="amo\npuella\n").splitlines()]
        assert len(batch) == 2
        assert all(document["schemaVersion"] == 4 for document in batch)

print("human CLI projection checks passed")
