import assert from "node:assert/strict";
import test from "node:test";
import {selectNpmDistTag} from "../scripts/select-npm-dist-tag.mjs";

test("selects latest for a stable GitHub Release", () => {
  assert.deepEqual(selectNpmDistTag("1.0.0", false), {
    PACKAGE_VERSION: "1.0.0",
    NPM_TAG: "latest",
  });
});

test("selects next for a SemVer prerelease", () => {
  assert.deepEqual(selectNpmDistTag("1.0.0-rc2", true), {
    PACKAGE_VERSION: "1.0.0-rc2",
    NPM_TAG: "next",
  });
});

test("rejects mismatched GitHub Release state", () => {
  for (const [version, prerelease] of [["1.0.0", true], ["1.0.0-rc2", false]]) {
    assert.throws(
      () => selectNpmDistTag(version, prerelease),
      /does not match SemVer tag/,
    );
  }
});

test("rejects a non-SemVer release tag", () => {
  assert.throws(
    () => selectNpmDistTag("release-candidate", true),
    /version is not SemVer/,
  );
});
