#!/usr/bin/env node
"use strict";

const {spawnSync} = require("node:child_process");
const {dirname, join} = require("node:path");

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
  console.error(`wordswasm: unsupported platform ${key}`);
  process.exitCode = 1;
  return;
}

let packageJson;
try {
  packageJson = require.resolve(`${packageName}/package.json`);
} catch {
  console.error(
    `wordswasm: ${packageName} is unavailable; reinstall without --omit=optional`,
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
const args = process.argv.slice(2);
if (!args.includes("--database")) {
  args.unshift("--database", join(packageRoot, "data", "words-full.wwdb"));
}
if (!args.includes("--format")) {
  args.unshift("--format", "analysis-v3");
}

const result = spawnSync(binary, args, {stdio: "inherit"});
if (result.error !== undefined) {
  console.error(`wordswasm: ${result.error.message}`);
  process.exitCode = 1;
} else if (result.signal !== null) {
  process.kill(process.pid, result.signal);
} else {
  process.exitCode = result.status ?? 1;
}
