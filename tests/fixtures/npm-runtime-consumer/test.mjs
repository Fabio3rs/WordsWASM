import assert from "node:assert/strict";
import {spawnSync} from "node:child_process";
import {readFile} from "node:fs/promises";
import {fileURLToPath} from "node:url";
import {chromium} from "playwright";
import {build, preview} from "vite";
import {checkEngine} from "./checks.js";

const version = process.env.CONSUMER_VERSION;
assert.ok(version, "Set CONSUMER_VERSION to the exact installed release");
for (const name of ["wordswasm", "wordswasm-cli"]) {
  const metadata = JSON.parse(await readFile(new URL(`node_modules/${name}/package.json`, import.meta.url)));
  assert.equal(metadata.version, version);
}
await checkEngine();
console.log(`Node ${process.version}: WASM full/search, initialization errors and dispose passed`);
const cli = fileURLToPath(new URL("node_modules/wordswasm-cli/bin/wordswasm.cjs", import.meta.url));
function run(args, input) {
  const result = spawnSync(process.execPath, [cli, ...args], {encoding: "utf8", input});
  assert.equal(result.status, 0, result.stderr);
  return result.stdout;
}
assert.equal(run(["--version"]).trim(), `wordswasm-cli ${version}`);
for (const format of ["analysis-v4", "search-v4"]) {
  const single = JSON.parse(run(["--format", format, "amo"]));
  const stream = run(["--format", format, "--input", "-"], "amo\npuella\n")
    .trim().split("\n").map(JSON.parse);
  assert.equal(stream.length, 2);
  for (const document of [single, ...stream]) {
    assert.equal(document.schemaVersion, 4);
    assert.equal(document.schema, `whitakers-words.${format.split("-")[0]}`);
  }
}
console.log(`${process.platform}/${process.arch}: installed npm CLI v4 passed`);
await build({build: {target: "esnext"}, worker: {format: "es"}});
const server = await preview({preview: {host: "127.0.0.1", port: 0}});
let browser;
try {
  browser = await chromium.launch(process.env.PLAYWRIGHT_EXECUTABLE_PATH
    ? {executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH} : {});
  const page = await browser.newPage();
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("console", (message) => { if (message.type() === "error") errors.push(message.text()); });
  page.on("requestfailed", (request) => errors.push(`${request.url()}: ${request.failure()?.errorText}`));
  page.on("response", (response) => { if (response.status() >= 400) errors.push(`${response.status()}: ${response.url()}`); });
  await page.goto(`http://127.0.0.1:${server.httpServer.address().port}`);
  await page.waitForFunction(() => window.consumerResult && window.consumerResult.state !== "running", null, {timeout: 120000});
  const result = await page.evaluate(() => window.consumerResult);
  assert.equal(result.state, "passed", result.error);
  assert.deepEqual(errors, []);
  console.log(`Chromium ${browser.version()}: browser and module Worker passed; no asset or console errors`);
} finally {
  await browser?.close();
  await new Promise((resolve, reject) => server.httpServer.close((error) => error ? reject(error) : resolve()));
}
