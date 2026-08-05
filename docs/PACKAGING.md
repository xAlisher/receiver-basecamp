# Packaging & catalog release — the `--impure` workaround (#80)

The receiver bundles its own per-system helpers (ffplay + tor + privoxy + libpulse/libtorsocks) into the
`.lgx` for zero-install (#75). This note explains why the build currently needs `--impure`, how catalog
releases work around it, and the upstream fix that will drop the workaround.

## Why the flake needs `--impure`

The bundle is injected via `mkLogosQmlModule`'s `postInstall` hook. That hook is a **single string, baked
once** — outside the builder's per-system loop. To pick the **right per-system bundle** (x86_64-linux vs
aarch64-darwin binaries) from that one string, `flake.nix` selects it with **`builtins.currentSystem`**,
which is **impure**. So every build must pass `--impure`:

```bash
nix build .#lgx-portable --impure       # linux-amd64 on x86_64-linux
```

A plain `nix build .#lgx-portable` fails pure eval with `error: attribute 'currentSystem' missing`.
The `/release` skill already uses `--impure`; keep doing so until the fix below lands.

## Consequence: the catalog CI can't auto-build → manual propagation

The modules-catalog release action (`Release receiver-basecamp` in `xAlisher/logos-basecamp-modules`)
builds each variant **purely** on its native runner, so the flake fails to evaluate and the workflow
fails. Receiver releases therefore propagate to the catalog **manually** (this is exactly what v0.3.0
shipped):

1. Build **and sign** each platform's `.lgx` — `linux-amd64` on x86_64-linux, `darwin-arm64` on an M1
   (`nix build .#lgx-portable --impure`). See the `/release` flow.
2. `lgx merge` them into one multi-variant `.lgx`, then **sign** the merged file.
3. `gh release create receiver_ui-v<ver>` in `xAlisher/logos-basecamp-modules` (`--target main`) with the
   signed `.lgx` + a `sidecar.json` (Basecamp 0.2.1+ **rejects unsigned** catalog packages).
4. `gh workflow run "Rebuild index"` → `index.json` picks up the new version (rescans every release's `.lgx`).

`linux-arm64` is omitted from these releases (the bundle only covers x86_64-linux + aarch64-darwin so far).

## The fix in flight — `logos-co/logos-module-builder` PR #184

The builder PR makes `postInstall` accept a **function `{ system, pkgs }: string`**, evaluated per-system
(mirrors the existing `preConfigure` handling) — so no `currentSystem`, pure eval. **Verified**: a receiver
build against that branch with

```nix
postInstall = { system, pkgs }: ''
  mkdir -p $out/lib/bin
  cp -a ${helperBundleFor system}/. $out/lib/bin/   # helperBundleFor = nixpkgs-2405 bundle for `system`
'';
```

builds with `nix build .#lgx-portable` — **no `--impure`** — and produces the correctly-bundled `.lgx`.

### When #184 merges — adopt it (drops this whole workaround)

1. `nix flake lock --update-input logos-module-builder` (to the merged rev).
2. In `flake.nix`, replace the `system = builtins.currentSystem;` block + string `postInstall` with the
   **function** form above (`helperBundleFor system` built from `nixpkgs-2405.legacyPackages.${system}`).
3. Build **pure** (`nix build .#lgx-portable`) — the `Release receiver-basecamp` catalog workflow auto-builds
   again → delete manual steps §2–§4 above; a normal `/release` propagates everything.
4. Optionally add `aarch64-linux` (linux-arm64) to `nix/helper-bundle.nix` for full variant parity.

## See also
- #75 self-contained bundling · #77 flake-native `postInstall` injection · #81 bundled-deps security · `docs/BUNDLED-DEPS.md`
