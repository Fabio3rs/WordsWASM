import {copyFile, mkdir, readFile, rm, stat, writeFile} from "node:fs/promises";
import {createHash} from "node:crypto";
import {promisify} from "node:util";
import {brotliCompress, constants, gzip} from "node:zlib";
import path from "node:path";

const compressBrotli = promisify(brotliCompress);
const compressGzip = promisify(gzip);

function parseArgs(argv) {
  const options = {
    buildDir: "build/wasm",
    bundle: undefined,
    fullDatabase: "whitakers-words/poc/compact-db/output/words-poc-dense.wwdb",
    searchDatabase: undefined,
    outDir: "dist/words-web",
    compress: true,
  };
  for (let index = 0; index < argv.length; ++index) {
    const argument = argv[index];
    if (argument === "--build-dir") {
      options.buildDir = argv[++index];
    } else if (argument === "--bundle") {
      options.bundle = argv[++index];
    } else if (argument === "--full-database") {
      options.fullDatabase = argv[++index];
    } else if (argument === "--search-database") {
      options.searchDatabase = argv[++index];
    } else if (argument === "--out-dir") {
      options.outDir = argv[++index];
    } else if (argument === "--no-compress") {
      options.compress = false;
    } else if (argument === "--help" || argument === "-h") {
      options.help = true;
    } else {
      throw new Error(`unsupported argument: ${argument}`);
    }
  }
  return options;
}

function usage() {
  return [
    "usage: node scripts/export-wasm-assets.mjs [options]",
    "  --bundle full|search|both  database projections to export",
    "  --build-dir DIR   Emscripten build directory",
    "  --full-database FILE  WWDB with morphology and meanings",
    "  --search-database FILE  optional WWDB without meanings",
    "  --out-dir DIR     deployable output directory",
    "  --no-compress     omit browser-native .br/.gz representations",
  ].join("\n");
}

async function digest(file) {
  const bytes = await readFile(file);
  return {
    bytes: bytes.byteLength,
    sha256: createHash("sha256").update(bytes).digest("hex"),
  };
}

async function inspectWwdb(file, expectedProfile) {
  const bytes = await readFile(file);
  const magic = Buffer.from([0x57, 0x57, 0x44, 0x42, 0x0d, 0x0a, 0x1a, 0x0a]);
  if (bytes.byteLength < 24 || !bytes.subarray(0, 8).equals(magic)) {
    throw new Error(`invalid WWDB header: ${file}`);
  }
  const metadata = {
    major: bytes.readUInt16LE(8),
    minor: bytes.readUInt16LE(10),
    profile: bytes.readUInt32LE(20),
  };
  if (metadata.profile !== expectedProfile) {
    throw new Error(
      `unexpected WWDB profile ${metadata.profile} in ${file}; ` +
        `expected ${expectedProfile}`,
    );
  }
  return metadata;
}

function canonicalJson(value) {
  if (Array.isArray(value)) {
    return `[${value.map(canonicalJson).join(",")}]`;
  }
  if (value !== null && typeof value === "object") {
    return `{${Object.keys(value).sort().map((key) =>
      `${JSON.stringify(key)}:${canonicalJson(value[key])}`).join(",")}}`;
  }
  return JSON.stringify(value);
}

async function exists(file) {
  try {
    return (await stat(file)).isFile();
  } catch (error) {
    if (error?.code === "ENOENT") {
      return false;
    }
    throw error;
  }
}

async function compressAsset(file) {
  const bytes = await readFile(file);
  // WHY: these are HTTP content codings understood by browsers. They remain
  // transport representations; Fetch exposes the original WWDB/WASM bytes.
  const brotli = await compressBrotli(bytes, {
    params: {[constants.BROTLI_PARAM_QUALITY]: 11},
  });
  const gzipped = await compressGzip(bytes, {level: 9, mtime: 0});
  await writeFile(`${file}.br`, brotli);
  await writeFile(`${file}.gz`, gzipped);
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    console.log(usage());
    return;
  }

  const bundle = options.bundle ??
    (options.searchDatabase === undefined ? "full" : "both");
  if (!["full", "search", "both"].includes(bundle)) {
    throw new Error(`unsupported bundle: ${bundle}`);
  }
  if ((bundle === "search" || bundle === "both") &&
      options.searchDatabase === undefined) {
    throw new Error(`--bundle ${bundle} requires --search-database`);
  }

  const root = process.cwd();
  const buildDir = path.resolve(root, options.buildDir);
  const outDir = path.resolve(root, options.outDir);
  const fullDatabase = bundle === "search"
    ? undefined
    : path.resolve(root, options.fullDatabase);
  const searchDatabase = options.searchDatabase === undefined
    ? undefined
    : path.resolve(root, options.searchDatabase);
  const required = [
    [path.join(buildDir, "words_wasm.mjs"), "words_wasm.mjs"],
    [path.join(buildDir, "words_wasm.wasm"), "words_wasm.wasm"],
    [path.join(buildDir, "words_wasm.d.ts"), "words_wasm.d.ts"],
    [path.join(buildDir, "words-engine.mjs"), "words-engine.mjs"],
    [path.join(buildDir, "words-engine.d.ts"), "words-engine.d.ts"],
    [path.join(buildDir, "words-engine.d.mts"), "words-engine.d.mts"],
  ];
  if (fullDatabase !== undefined) {
    required.push([fullDatabase, "words-full.wwdb"]);
  }
  if (bundle !== "full" && searchDatabase !== undefined) {
    required.push([searchDatabase, "words-search.wwdb"]);
  }
  const optional = required.slice(0, 5).flatMap(([source, name]) => [
    [`${source}.br`, `${name}.br`],
    [`${source}.gz`, `${name}.gz`],
  ]);

  for (const [source] of required) {
    if (!await exists(source)) {
      throw new Error(`required asset not found: ${source}`);
    }
  }
  await mkdir(outDir, {recursive: true});
  // Keep repeated exports exact: switching an existing directory from
  // `both` to `search` must not leave a stale full database available.
  const managedNames = [
    "words_wasm.mjs",
    "words_wasm.wasm",
    "words_wasm.d.ts",
    "words-engine.mjs",
    "words-engine.d.ts",
    "words-engine.d.mts",
    "words-full.wwdb",
    "words-search.wwdb",
  ];
  await Promise.all([
    "dataset-manifest.json",
    "manifest.json",
    ...managedNames.flatMap((name) => [name, `${name}.br`, `${name}.gz`]),
  ].map((name) => rm(path.join(outDir, name), {force: true})));

  const fullFormat = fullDatabase === undefined
    ? undefined
    : await inspectWwdb(fullDatabase, 2);
  const searchFormat = bundle === "full" || searchDatabase === undefined
    ? undefined
    : await inspectWwdb(searchDatabase, 4);
  if (fullFormat !== undefined && searchFormat !== undefined &&
      (searchFormat.major !== fullFormat.major ||
       searchFormat.minor !== fullFormat.minor)) {
    throw new Error("full and search WWDB files use different format versions");
  }

  // WHY: full and future search/meanings containers must share the ID space,
  // so datasetId hashes canonical sources and packer semantics rather than
  // the bytes of any one physical projection.
  const identitySourceNames = [
    "whitakers-words/DICTLINE.GEN",
    "whitakers-words/INFLECTS.LAT",
    "whitakers-words/ADDONS.LAT",
    "whitakers-words/UNIQUES.LAT",
    "whitakers-words/REWRITES.LAT",
    "whitakers-words/QUANTITIES.LAT",
    "whitakers-words/poc/compact-db/wwdb_poc_pack.cpp",
  ];
  const optionalIdentitySourceNames = [
    "whitakers-words/LEXEMES.LAT",
  ];
  // WHY: an enriched full/search pair must never reuse the identity of its
  // legacy-only predecessor. LEXEMES.LAT is optional so an uncurated checkout
  // continues to reproduce the original dataset exactly.
  for (const name of optionalIdentitySourceNames) {
    if (await exists(path.resolve(root, name))) {
      identitySourceNames.push(name);
    }
  }
  const sources = {};
  for (const name of identitySourceNames) {
    sources[name] = await digest(path.resolve(root, name));
  }
  const wwdbFormat = fullFormat ?? searchFormat;
  const datasetManifest = {
    schema: "whitakers-words.dataset",
    schemaVersion: 1,
    idSpace: "wwdb-dense-ids-v1",
    // WHY: derive this from the artifact so release metadata cannot silently
    // lag behind a compatible format evolution in the native packer.
    wwdbFormat: {major: wwdbFormat.major, minor: wwdbFormat.minor},
    sources,
  };
  const datasetId = `sha256:${createHash("sha256")
    .update(canonicalJson(datasetManifest))
    .digest("hex")}`;
  await writeFile(
    path.join(outDir, "dataset-manifest.json"),
    `${JSON.stringify(datasetManifest, null, 2)}\n`,
    "utf8",
  );

  const copied = new Set();
  for (const [source, name] of required) {
    await copyFile(source, path.join(outDir, name));
    copied.add(name);
  }
  for (const [source, name] of optional) {
    if (await exists(source)) {
      await copyFile(source, path.join(outDir, name));
      copied.add(name);
    }
  }
  if (options.compress) {
    // Compress from the final copied bytes so databases and generated glue
    // follow exactly the same release path regardless of CMake tooling.
    for (const [, name] of required) {
      await compressAsset(path.join(outDir, name));
      copied.add(`${name}.br`);
      copied.add(`${name}.gz`);
    }
  }
  copied.add("dataset-manifest.json");

  const files = {};
  for (const name of [...copied].sort()) {
    files[name] = await digest(path.join(outDir, name));
  }
  const databases = {};
  if (fullDatabase !== undefined) {
    databases.full = {
      file: "words-full.wwdb",
      layoutProfile: "dense",
      provides: ["analysis", "search"],
    };
  }
  if (bundle !== "full" && searchDatabase !== undefined) {
    databases.search = {
      file: "words-search.wwdb",
      layoutProfile: "search-only",
      provides: ["search"],
    };
  }
  const manifest = {
    schema: "whitakers-words.web-assets",
    schemaVersion: 1,
    datasetId,
    datasetManifest: "dataset-manifest.json",
    entrypoint: "words-engine.mjs",
    databases,
    files,
  };
  await writeFile(
    path.join(outDir, "manifest.json"),
    `${JSON.stringify(manifest, null, 2)}\n`,
    "utf8",
  );
  console.log(JSON.stringify({ok: true, outDir, datasetId}));
}

await main();
