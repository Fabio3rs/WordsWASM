import {readFile, realpath} from "node:fs/promises";
import {spawnSync} from "node:child_process";
import path from "node:path";

function parseArgs(argv) {
  const options = {plan: undefined, userconfig: undefined};
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--userconfig") {
      options.userconfig = argv[++index];
    } else if (options.plan === undefined) {
      options.plan = argument;
    } else {
      throw new Error(`unsupported argument: ${argument}`);
    }
  }
  if (options.plan === undefined) {
    throw new Error("usage: node scripts/bootstrap-publish-npm.mjs PLAN [--userconfig FILE]");
  }
  return options;
}

function npm(args, userconfig) {
  const completeArgs = userconfig === undefined ? args : [...args, "--userconfig", userconfig];
  return spawnSync("npm", completeArgs, {encoding: "utf8"});
}

function output(result) {
  return `${result.stdout ?? ""}${result.stderr ?? ""}`;
}

function isMissing(result) {
  return result.status !== 0 && /\bE404\b/.test(output(result));
}

function value(result) {
  return JSON.parse(result.stdout);
}

async function lookup(name, version, userconfig) {
  const result = npm(["view", `${name}@${version}`, "dist.integrity", "--json"], userconfig);
  if (result.status === 0) return {state: "present", integrity: value(result)};
  if (isMissing(result)) return {state: "missing"};
  throw new Error(`could not query ${name}@${version}:\n${output(result)}`);
}

async function lookupAfterPropagation(name, version, userconfig) {
  // Registry reads can lag behind a successful publish.  In particular, a
  // retry of a partially completed release must not mistake a just-published
  // version for a missing one and attempt to publish it again.
  const delays = [1_000, 2_000, 4_000, 8_000];
  for (let attempt = 0; attempt <= delays.length; attempt += 1) {
    const result = await lookup(name, version, userconfig);
    if (result.state === "present" || attempt === delays.length) return result;
    await new Promise((resolve) => setTimeout(resolve, delays[attempt]));
  }
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const planPath = path.resolve(options.plan);
  const planDirectory = path.dirname(planPath);
  const plan = JSON.parse(await readFile(planPath, "utf8"));

  for (const item of plan.packages) {
    const tarball = await realpath(path.join(planDirectory, item.tarball));
    console.log(`\n${item.name}@${plan.version}`);
    console.log(`  tarball: ${tarball}`);
    const remote = await lookupAfterPropagation(item.name, plan.version, options.userconfig);
    if (remote.state === "present") {
      if (remote.integrity !== item.integrity) {
        throw new Error(`  ${item.name}@${plan.version} has a different published tarball`);
      }
      console.log("  status: already published");
    } else {
      console.log("  status: publishing");
      const published = npm(["publish", tarball, "--tag", plan.tag, "--access", "public"], options.userconfig);
      if (published.status !== 0) {
        const afterFailure = await lookupAfterPropagation(item.name, plan.version, options.userconfig);
        if (afterFailure.state !== "present" || afterFailure.integrity !== item.integrity) {
          throw new Error(`  publication failed for ${item.name}:\n${output(published)}`);
        }
        console.log("  status: already published while the registry propagated");
      }
    }
  }
}

await main();
