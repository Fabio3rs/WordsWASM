import {mkdir, readFile, stat, writeFile} from "node:fs/promises";
import {spawnSync} from "node:child_process";
import path from "node:path";

function run(command, args, cwd) {
  const result = spawnSync(command, args, {cwd, encoding: "utf8"});
  if (result.status !== 0) {
    throw new Error(
      `${command} ${args.join(" ")} failed:\n${result.error?.message ?? ""}${result.stdout}${result.stderr}`,
    );
  }
  return result.stdout;
}

const outDir = path.resolve(process.cwd(), process.argv[2] ?? "release/npm");
const planFile = path.join(outDir, "publish-plan.json");
const plan = JSON.parse(await readFile(planFile, "utf8"));
const tarballs = path.join(outDir, "tarballs");
await mkdir(tarballs, {recursive: true});

for (const item of plan.packages) {
  const directory = path.join(outDir, item.directory);
  const preview = JSON.parse(run("npm", ["pack", "--dry-run", "--json"], directory));
  if (preview.length !== 1 || preview[0].name !== item.name || preview[0].version !== plan.version) {
    throw new Error(`unexpected npm pack preview for ${item.name}`);
  }
  if (preview[0].files.some((file) => file.path.endsWith(".br") || file.path.endsWith(".gz"))) {
    throw new Error(`${item.name} contains HTTP compression artifacts`);
  }
  const packed = JSON.parse(run("npm", ["pack", "--json", "--pack-destination", tarballs], directory));
  const tarball = packed[0];
  await stat(path.join(tarballs, tarball.filename));
  item.tarball = path.join("tarballs", tarball.filename);
  item.integrity = tarball.integrity;
}

await writeFile(planFile, `${JSON.stringify(plan, null, 2)}\n`);
