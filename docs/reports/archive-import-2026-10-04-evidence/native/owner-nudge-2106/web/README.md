# Isolated offline web build

The exact [build receipt](receipt.json), stdout/stderr, tool-version outputs and
package/lock files come from the isolated `thread5-web-check/web` build. No file
in the repository's `loom/web` was changed. All **79 source identities** match
before/after and also match the frozen current source reported by the owner
agent as main `30ad7d3`, W5 `4e8c3de`. These references describe the working
checkout; the actual byte manifests bind the build independently of a Git label.

Actual results: `npm ci --offline --ignore-scripts --no-audit --no-fund` exited
0, installed 76 packages; `npm run build` exited 0, transformed 85 modules and
produced **4 files**. Node 24.19.0, npm 11.9.0, TypeScript 5.9.3, Vite 5.4.21.
[asset-manifest.json](asset-manifest.json) retains their actual sizes and SHA-256
hashes; generated files are reproducible and are not copied into this bundle.
No browser/UI test or model call was performed. These results are build evidence,
not a claim that every UI flow works.

Both commands had `NPM_CONFIG_OFFLINE=true`; installation additionally used
`--offline` and disabled lifecycle scripts, audit and funding requests. No
network installation fallback was needed. **There is no measured zero-syscall
network claim:** the receipt's syscall count is null because container policy
prohibited `ptrace`. [strace-negative-setup/](strace-negative-setup/) preserves
that separate setup failure. **npm did not execute in the traced setup**;
its exit 1 was the tracer's failure, not a failed package installation or build.

Source JSON manifests are losslessly stored as `source-before.json.gz` and
`source-after.json.gz`, preserving their exact original bytes. Their original
names/hashes in the unchanged receipt refer to those uncompressed bytes.
[SHA256.json](SHA256.json) binds both the compressed files and their uncompressed
identities, together with every retained log/receipt. To restore original files:

```bash
gzip -dk source-before.json.gz source-after.json.gz
```

Reproduce against a checkout matching the source manifest, in a fresh external
copy of `loom/web` (omit any prior `node_modules` or `dist`):

```bash
npm ci --offline --ignore-scripts --no-audit --no-fund
NPM_CONFIG_OFFLINE=true npm run build
```

Offline reproduction requires the matching packages in npm's local cache.
The original failed instrumentation and successful direct build remain separate;
no test, threshold, source or lockfile was changed to obtain success.
