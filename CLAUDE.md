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
