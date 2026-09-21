import {cp, mkdir, readFile, rm, writeFile} from "node:fs/promises";
import path from "node:path";

const platformPackages = [
  {suffix: "linux-x64", os: "linux", cpu: "x64", artifact: "words-cli-linux-x86_64", executable: "words_cli"},
  {suffix: "linux-arm64", os: "linux", cpu: "arm64", artifact: "words-cli-linux-arm64", executable: "words_cli"},
  {suffix: "linux-arm", os: "linux", cpu: "arm", artifact: "words-cli-linux-armhf", executable: "words_cli"},
  {suffix: "darwin-x64", os: "darwin", cpu: "x64", artifact: "words-cli-macos-x86_64", executable: "words_cli"},
  {suffix: "darwin-arm64", os: "darwin", cpu: "arm64", artifact: "words-cli-macos-arm64", executable: "words_cli"},
  {suffix: "win32-x64", os: "win32", cpu: "x64", artifact: "words-cli-windows-x86_64", executable: "words_cli.exe"},
];

function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (["--version", "--web-dir", "--native-dir", "--out-dir", "--tag"].includes(argument)) {
      options[argument.slice(2).replace(/-([a-z])/g, (_, letter) => letter.toUpperCase())] = argv[++index];
    } else {
      throw new Error(`unsupported argument: ${argument}`);
    }
  }
  for (const name of ["version", "webDir", "nativeDir", "outDir", "tag"]) {
    if (typeof options[name] !== "string" || options[name] === "") {
      throw new Error(`missing --${name.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`)}`);
    }
  }
  if (!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/.test(options.version)) {
    throw new Error(`version is not SemVer: ${options.version}`);
  }
  if (!/^[A-Za-z][A-Za-z0-9_-]*$/.test(options.tag)) {
    throw new Error(`invalid npm dist-tag: ${options.tag}`);
  }
  return options;
}

async function copy(source, destination) {
  await mkdir(path.dirname(destination), {recursive: true});
  await cp(source, destination, {force: true, recursive: true});
}

async function readJson(file) {
  return JSON.parse(await readFile(file, "utf8"));
}

async function writeJson(file, value) {
  await mkdir(path.dirname(file), {recursive: true});
  await writeFile(file, `${JSON.stringify(value, null, 2)}\n`);
}

function packageMetadata(template, version) {
  return {...template, version};
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const root = process.cwd();
  const webDir = path.resolve(root, options.webDir);
  const nativeDir = path.resolve(root, options.nativeDir);
  const outDir = path.resolve(root, options.outDir);
  const stagedDir = path.join(outDir, "staged");
  const source = path.join(root, "npm");
  await rm(outDir, {recursive: true, force: true});
  await mkdir(stagedDir, {recursive: true});

  const requiredWebFiles = [
    "words-engine.mjs", "words-engine.d.ts", "words-engine.d.mts",
    "words_wasm.mjs", "words_wasm.wasm", "words_wasm.d.ts",
    "words-full.wwdb", "words-search.wwdb", "manifest.json",
    "dataset-manifest.json",
  ];
  for (const file of requiredWebFiles) {
    await readFile(path.join(webDir, file));
  }

  const packages = [];
  for (const platform of platformPackages) {
    const name = `@fabiors/wordswasm-cli-${platform.suffix}`;
    const directory = path.join(stagedDir, `fabiors-wordswasm-cli-${platform.suffix}`);
    const metadata = {
      name,
      version: options.version,
      description: `WordsWASM native CLI for ${platform.os} ${platform.cpu}`,
      license: "MIT",
      repository: {type: "git", url: "git+https://github.com/Fabio3rs/WordsWASM.git"},
      bugs: "https://github.com/Fabio3rs/WordsWASM/issues",
      homepage: "https://github.com/Fabio3rs/WordsWASM#readme",
      os: [platform.os],
      cpu: [platform.cpu],
      publishConfig: {access: "public"},
      files: ["bin", "data", "README.md", "LICENSE"],
    };
    await writeJson(path.join(directory, "package.json"), metadata);
    await copy(path.join(source, "platform", "README.md"), path.join(directory, "README.md"));
    await copy(path.join(root, "LICENSE"), path.join(directory, "LICENSE"));
    await copy(
      path.join(nativeDir, platform.artifact, platform.executable),
      path.join(directory, "bin", platform.executable),
    );
    await copy(path.join(webDir, "words-full.wwdb"), path.join(directory, "data", "words-full.wwdb"));
    packages.push({name, directory: path.relative(outDir, directory), kind: "platform"});
  }

  const cliDirectory = path.join(stagedDir, "wordswasm-cli");
  const cliTemplate = await readJson(path.join(source, "wordswasm-cli", "package.json"));
  const cliMetadata = packageMetadata(cliTemplate, options.version);
  cliMetadata.optionalDependencies = Object.fromEntries(
    platformPackages.map((platform) => [
      `@fabiors/wordswasm-cli-${platform.suffix}`,
      options.version,
    ]),
  );
  await writeJson(path.join(cliDirectory, "package.json"), cliMetadata);
  await copy(path.join(source, "wordswasm-cli", "README.md"), path.join(cliDirectory, "README.md"));
  await copy(path.join(source, "wordswasm-cli", "bin"), path.join(cliDirectory, "bin"));
  await copy(path.join(root, "LICENSE"), path.join(cliDirectory, "LICENSE"));
  packages.push({name: "wordswasm-cli", directory: path.relative(outDir, cliDirectory), kind: "wrapper"});

  const wasmDirectory = path.join(stagedDir, "wordswasm");
  const wasmTemplate = await readJson(path.join(source, "wordswasm", "package.json"));
  await writeJson(path.join(wasmDirectory, "package.json"), packageMetadata(wasmTemplate, options.version));
  await copy(path.join(source, "wordswasm", "README.md"), path.join(wasmDirectory, "README.md"));
  await copy(path.join(root, "LICENSE"), path.join(wasmDirectory, "LICENSE"));
  await copy(path.join(source, "wordswasm", "index.mjs"), path.join(wasmDirectory, "dist", "index.mjs"));
  await copy(path.join(source, "wordswasm", "index.d.mts"), path.join(wasmDirectory, "dist", "index.d.mts"));
  for (const file of requiredWebFiles) {
    await copy(path.join(webDir, file), path.join(wasmDirectory, "dist", file));
  }
  packages.push({name: "wordswasm", directory: path.relative(outDir, wasmDirectory), kind: "wasm"});

  await writeJson(path.join(outDir, "publish-plan.json"), {
    version: options.version,
    tag: options.tag,
    packages,
  });
}

await main();
