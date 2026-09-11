import {readFile} from "node:fs/promises";
import {brotliCompressSync, constants, gzipSync} from "node:zlib";
import {createHash} from "node:crypto";

if (process.argv.length < 3) {
  console.error("usage: node measure_compression.mjs ARTIFACT...");
  process.exitCode = 2;
} else {
  for (const path of process.argv.slice(2)) {
    const bytes = await readFile(path);
    const gzip = gzipSync(bytes, {level: 9, mtime: 0});
    const brotli = brotliCompressSync(bytes, {
      params: {
        [constants.BROTLI_PARAM_QUALITY]: 11,
      },
    });
    console.log(JSON.stringify({
      path,
      sha256: createHash("sha256").update(bytes).digest("hex"),
      rawBytes: bytes.length,
      gzip9Bytes: gzip.length,
      brotli11Bytes: brotli.length,
    }));
  }
}
