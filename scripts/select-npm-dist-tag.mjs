import {pathToFileURL} from "node:url";

function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--version" || argument === "--release-prerelease") {
      options[argument.slice(2).replace(/-([a-z])/g, (_, letter) => letter.toUpperCase())] = argv[++index];
    } else {
      throw new Error(`unsupported argument: ${argument}`);
    }
  }
  if (typeof options.version !== "string" || options.version === "") {
    throw new Error("missing --version");
  }
  if (!["true", "false"].includes(options.releasePrerelease)) {
    throw new Error("--release-prerelease must be true or false");
  }
  return options;
}

const semver = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/;

export function selectNpmDistTag(version, releasePrerelease) {
  if (!semver.test(version)) {
    throw new Error(`version is not SemVer: ${version}`);
  }
  const tagIsPrerelease = version.includes("-");
  if (tagIsPrerelease !== releasePrerelease) {
    throw new Error(
      `release pre-release state (${releasePrerelease}) does not match SemVer tag ${version}`,
    );
  }
  return {PACKAGE_VERSION: version, NPM_TAG: tagIsPrerelease ? "next" : "latest"};
}

if (process.argv[1] !== undefined &&
    import.meta.url === pathToFileURL(process.argv[1]).href) {
  const options = parseArgs(process.argv.slice(2));
  const selected = selectNpmDistTag(
    options.version,
    options.releasePrerelease === "true",
  );
  process.stdout.write(`PACKAGE_VERSION=${selected.PACKAGE_VERSION}\n`);
  process.stdout.write(`NPM_TAG=${selected.NPM_TAG}\n`);
}
