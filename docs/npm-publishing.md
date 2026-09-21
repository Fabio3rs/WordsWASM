# Publishing npm packages

The first publication is a local bootstrap. It creates the npm package records
without storing an npm credential in GitHub. Later releases publish through
GitHub Actions using npm Trusted Publishing and OIDC.

## Bootstrap a pre-release

1. Commit this repository state and push it.
2. In GitHub's **Releases** page, create and publish the pre-release tag. Mark it as
   **Set as a pre-release**. Publishing the release starts the `Native and
   WebAssembly build` workflow, which assigns `next` to this version.
3. When that workflow succeeds, download its `npm-packages` artifact and
   extract it. The artifact contains eight inspected tarballs and
   `publish-plan.json`.
4. Authenticate locally and use a temporary granular access token with
   publishing permission and **bypass 2FA** enabled. On npm's current direct
   publishing flow, an account with publishing 2FA alone may return `E403`
   without presenting an interactive challenge.

   ```sh
   npm login
   npm whoami
   export NPM_TOKEN='temporary-token-value'
   tmp="$(mktemp)"
   chmod 600 "$tmp"
   printf '%s\n' \
     'registry=https://registry.npmjs.org/' \
     "//registry.npmjs.org/:_authToken=${NPM_TOKEN}" > "$tmp"
   ```

5. Inspect the extracted artifact, then run the idempotent bootstrap helper
   from a checkout of this repository. It resolves each tarball to an absolute
   local path, preserves the order in `publish-plan.json`, and logs whether a
   package was published or already existed.

   ```sh
   for tarball in tarballs/*.tgz; do
     tar -tzf "$tarball"
   done

   node scripts/bootstrap-publish-npm.mjs \
     /absolute/path/to/npm-packages/publish-plan.json \
     --userconfig "$tmp"
   ```

   The helper passes `--access public` explicitly and removes `latest` when an
   initial pre-release publication caused npm to create that tag too.

6. Remove the temporary credential and revoke the granular token in npm:

   ```sh
   rm -f "$tmp"
   unset tmp NPM_TOKEN
   ```

7. Confirm the public tags:

   ```sh
   npm dist-tag ls wordswasm
   npm dist-tag ls wordswasm-cli
   ```

## Enable trusted publishing

The packages must exist before npm can trust a workflow. Use npm 11.15 or
newer, then run the following after the bootstrap publish:

```sh
packages=(
  wordswasm
  wordswasm-cli
  @fabiors/wordswasm-cli-linux-x64
  @fabiors/wordswasm-cli-linux-arm64
  @fabiors/wordswasm-cli-linux-arm
  @fabiors/wordswasm-cli-darwin-x64
  @fabiors/wordswasm-cli-darwin-arm64
  @fabiors/wordswasm-cli-win32-x64
)

for package in "${packages[@]}"; do
  npm trust github "$package" \
    --repo Fabio3rs/WordsWASM \
    --file npm-publish.yml \
    --allow-publish \
    --yes
  sleep 2
done
```

Alternatively, configure the same GitHub repository and
`.github/workflows/npm-publish.yml` in every package's npm **Settings → Trusted
Publishing** page.

The initial `npm-publish.yml` run has no npm trust configuration and will fail.
That is expected. After all eight packages trust the workflow, re-run it in the
GitHub Actions interface. It verifies that the locally published tarballs and
their `next` tags match the build artifact, then succeeds without republishing.

## Later releases

Publish a GitHub prerelease whose SemVer tag has a prerelease suffix, such as
`v1.0.0-rc2`, to build and publish `next`. Publish a stable `vX.Y.Z` GitHub
Release to publish `latest`. The **Pre-release** checkbox and the tag must
agree; the build rejects a mismatch so an RC cannot become `latest` by
accident. A stable publication does not move `next`, which continues to point
to the most recent prerelease until another prerelease is published.

The release receives its npm identity from GitHub OIDC; it deliberately does
not use `NPM_TOKEN`, `NODE_AUTH_TOKEN`, or another npm publishing secret. See
[versioning and compatibility](versioning.md) for the stable API baseline and
SemVer policy.
