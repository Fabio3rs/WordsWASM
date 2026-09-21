# Publishing npm packages

The first publication is a local bootstrap. It creates the npm package records
without storing an npm credential in GitHub. Later releases publish through
GitHub Actions using npm Trusted Publishing and OIDC.

## First release: `1.0.0-rc2`

1. Commit this repository state and push it.
2. In GitHub's **Releases** page, create and publish `v1.0.0-rc2`. Mark it as
   **Set as a pre-release**. Publishing the release starts the `Native and
   WebAssembly build` workflow, which assigns `next` to this version.
3. When that workflow succeeds, download its `npm-packages` artifact and
   extract it. The artifact contains eight inspected tarballs and
   `publish-plan.json`.
4. With an npm account that has a verified email and publishing 2FA enabled,
   authenticate locally:

   ```sh
   npm login
   npm whoami
   ```

5. Change into the extracted artifact directory. Inspect its contents, then
   publish the tarballs in the recorded dependency order:

   ```sh
   for tarball in tarballs/*.tgz; do
     tar -tzf "$tarball"
   done

   node -e '
   const plan = require("./publish-plan.json");
   for (const item of plan.packages) console.log(item.tarball);
   ' | while IFS= read -r tarball; do
     npm publish "$tarball" --tag next
   done
   ```

   The first six tarballs are the public `@fabiors/*` platform packages. Their
   package metadata contains `publishConfig.access: public`, so no token or
   separate access flag is needed in this command.

6. Confirm the public tags:

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

Publish a GitHub pre-release to build and publish `next`. Publish a normal
GitHub release to build and publish `latest`. The workflow uses the release's
**Pre-release** checkbox, rather than trying to infer a channel from the tag
name. It receives its npm identity from GitHub OIDC; it deliberately does not
use `NPM_TOKEN`, `NODE_AUTH_TOKEN`, or another npm publishing secret.
