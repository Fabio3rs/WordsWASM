#!/usr/bin/env node
"use strict";

const {spawnSync} = require("node:child_process");
const {dirname, join} = require("node:path");

const args = process.argv.slice(2);
const packageVersion = require("../package.json").version;

function usage() {
  console.log(`WordsWASM command-line interface

Usage:
  wordswasm [OPTIONS] LATIN_TEXT ...
  wordswasm [OPTIONS] --input FILE
  wordswasm [OPTIONS] < queries.txt

Examples:
  wordswasm mālum
  wordswasm --pretty "amo puellam"
  printf 'amo\\npuella\\n' | wordswasm
  wordswasm --db /path/to/words-search.wwdb --format search-v3 mālum

Defaults:
  Uses the bundled full database and compact analysis-v3 JSON output.

Options:
  --database FILE, --db FILE  Use another WWDB database.
  --format FORMAT, -f FORMAT  analysis-v3 (default) or search-v3.
  --pretty                    Indent JSON for terminal reading; emit an array for multiple results.
  -i FILE, --input FILE       Read one query per line; use - for standard input.
                              With no text and no --input, read from standard input.
  --batch-json-lines, --batch Legacy aliases for --input -.
  --dataset-id ID             Verify the database dataset identifier.
  --two-words=legacy          Choose the legacy two-word behavior.
  --orthography=MODE          disabled, classical, or medieval.
  --no-fixes, --no-prefixes, --no-suffixes, --no-tickons, --no-tackons,
  --no-packons, --no-syncope, --no-verbal-compounds
                              Disable individual analysis mechanisms.
  --help, -h                  Show this help.
  --version                   Show the wrapper package version.

JSON is written to stdout; diagnostics are written to stderr. --pretty remains
valid JSON, but cannot be used with stream input because JSONL needs one compact
JSON value per line. The native command exits 2 for invalid input, 3 for
database/engine/input errors, and 4 for unexpected failures.
`);
}

if (args.includes("--help") || args.includes("-h")) {
  usage();
  process.exitCode = 0;
  return;
}
if (args.includes("--version")) {
  console.log(`wordswasm-cli ${packageVersion}`);
  process.exitCode = 0;
  return;
}

const platforms = new Map([
  ["linux-x64", "@fabiors/wordswasm-cli-linux-x64"],
  ["linux-arm64", "@fabiors/wordswasm-cli-linux-arm64"],
  ["linux-arm", "@fabiors/wordswasm-cli-linux-arm"],
  ["darwin-x64", "@fabiors/wordswasm-cli-darwin-x64"],
  ["darwin-arm64", "@fabiors/wordswasm-cli-darwin-arm64"],
  ["win32-x64", "@fabiors/wordswasm-cli-win32-x64"],
]);

const key = `${process.platform}-${process.arch}`;
const packageName = platforms.get(key);
if (packageName === undefined) {
  console.error(
    `wordswasm: ${key} is not supported by the npm CLI package. ` +
    "Download a standalone build from https://github.com/Fabio3rs/WordsWASM/releases.",
  );
  process.exitCode = 1;
  return;
}

let packageJson;
try {
  packageJson = require.resolve(`${packageName}/package.json`);
} catch {
  console.error(
    `wordswasm: the native package ${packageName} is unavailable. ` +
    "Reinstall wordswasm-cli without --omit=optional (or --no-optional).",
  );
  process.exitCode = 1;
  return;
}

const packageRoot = dirname(packageJson);
const binary = join(
  packageRoot,
  "bin",
  process.platform === "win32" ? "words_cli.exe" : "words_cli",
);
const hasOption = (...names) => args.some((argument) =>
  names.some((name) => argument === name || argument.startsWith(`${name}=`)),
);
if (!hasOption("--database", "--db")) {
  args.unshift("--database", join(packageRoot, "data", "words-full.wwdb"));
}
if (!hasOption("--format", "-f")) {
  args.unshift("--format", "analysis-v3");
}

const result = spawnSync(binary, args, {stdio: "inherit"});
if (result.error !== undefined) {
  console.error(`wordswasm: could not start its native executable: ${result.error.message}`);
  process.exitCode = 1;
} else if (result.signal !== null) {
  process.kill(process.pid, result.signal);
} else {
  process.exitCode = result.status ?? 1;
}
