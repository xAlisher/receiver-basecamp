# receiver-basecamp — agent instructions

Read [`README.md`](README.md) first. **Works on the current `v0.2.0` build** (verified 2026-07-04) — the
old **268-only** caveat is **stale/resolved**: the v0.2 platform migration fixed the `getClient` hang
(logos-basecamp#150). Building/testing on macOS? Follow
[`docs/MACOS-BUILD-PROTOCOL.md`](docs/MACOS-BUILD-PROTOCOL.md) and report in issue #1.

## Always save the build log

Builds here are heavy (they compile `delivery_module` incl. zerokit/RLN) and failures are the whole
point of capturing — so **pipe every build through `tee` to a timestamped file** under `logs/`:

```bash
mkdir -p logs
nix build .#lgx-portable -L 2>&1 | tee "logs/build-$(date +%Y%m%d-%H%M%S).log"
# dev variant:   nix build .#lgx        -L 2>&1 | tee "logs/build-dev-$(date +%Y%m%d-%H%M%S).log"
# standalone:    nix run .                  2>&1 | tee "logs/run-$(date +%Y%m%d-%H%M%S).log"
```

Why: the gating unknown on macOS is **whether zerokit builds for `aarch64-darwin`**. If a build fails,
the saved log's tail *is* the finding — paste the relevant section into issue #1 (don't rely on scrollback;
`-L` output is long and gets truncated). `logs/` is git-ignored, so saving is free and never pollutes commits.

Don't report a build as passing/failing from memory — point at the saved log (see fieldcraft
`verify-before-claiming`): `📍 build — logs/build-….log shows "Done:" / the zerokit failure at line N`.

## Delivery network changes — validate against the STOCK module, then restart

Two rules learned the expensive way in #94 (v0.4.0 shipped broken to every user):

1. **Never name a preset the shipped `delivery_module` may not have.** The preset table is compiled into
   `liblogosdelivery.so` and differs per build — the module the package manager resolves (v1.1.0) has
   only `logos.dev`; `logos.test` exists only on some dev builds. Naming it → `createNode` rejected →
   *no node at all*, amber pill, nothing in any log. Select the fleet with explicit `entryNodes`
   instead (both networks are cluster 2). Verify before shipping, against the real module:
   ```bash
   cp -r ~/.local/share/Logos/LogosBasecamp/modules/delivery_module "$MDIR/"   # what a USER gets
   logoscore call delivery_module createNode "$CFG"      # must be success:true
   ```
2. **`createNode` runs once per delivery_module PROCESS.** Installing a network change into a running
   Basecamp keeps the old node (`createNode rejected - context already initialized`). A full quit +
   relaunch is mandatory — say so in the release notes. And run **one** Basecamp at a time: two
   instances with an empty `LOGOS_INSTANCE_ID` share the IPC namespace, so the panel you are looking at
   may not be the instance doing discovery.

Debugging is blind by default: this module's `log()` reaches **no** Basecamp log. Use `diag()` (writes
`/tmp/receiver-diag-<LOGOS_INSTANCE_ID>.log`) and log every early return **with its reason**.

**macOS builds are not wetware** — the M1 is SSH-reachable as `m1` (user `sher`):
`ssh m1 'cd ~/basecamp/modules/receiver-basecamp && git checkout <tag> && nix build .#lgx-portable --impure'`
(`.#lgx-portable`, never `.#lgx`), then sign on the Linux box and `lgx merge` both arches for the catalog.
