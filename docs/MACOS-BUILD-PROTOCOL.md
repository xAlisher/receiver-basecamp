# macOS/arm64 build & test protocol (for a Claude agent on a Mac)

Goal: produce and test a **macOS/arm64 (`aarch64-darwin`)** LGX of `receiver_ui`. This has never been
built on Darwin — it's gated on one big unknown (does `delivery_module`'s zerokit/RLN build for
arm64-darwin?) plus a runtime question (does the mac platform build hit the same `getClient` hang as
linux 295?). Follow this top to bottom and **report findings in the tracking issue** (link at bottom).

The linux side is done and proven: the module discovers + plays end-to-end on the **268** linux build;
the latest **295** linux build hangs `getClient("delivery_module")` (platform regression
[logos-basecamp#150](https://github.com/logos-co/logos-basecamp/issues/150)). Read
[`README.md`](../README.md) and [`PROJECT_KNOWLEDGE.md`](../PROJECT_KNOWLEDGE.md) first.

---

## ✅ RESOLVED (2026-06-12) — read this before following the steps

This protocol was written before the mac run. Outcomes of the four assumptions:

1. **zerokit/RLN builds on `aarch64-darwin`** — ✅ YES. `delivery_module` (pinned `main`) builds; not the blocker.
2. **`.#lgx-portable` emits a `darwin-arm64` variant with bundled dylibs** — ✅ YES. **Use it, not `.#lgx`**
   — the dev `.#lgx` (`darwin-arm64-dev`) ships no bundled libs and **silently won't load** for a C++ module.
3. **Mac platform `getClient` hang?** — ✅ NO hang here; `getClient` + request/reply work fine on mac.
4. **Tor playback?** — not reached; blocked by the real blocker below.

**The real blocker (new):** cross-module **events** (`delivery_module messageReceived` via `onEvent`)
**never dispatch on macOS** — in the ui-host AND in a `type:core` logos_host relay (49 emits → 0
callbacks). It's the `QRemoteObjectReplica` IPC boundary (CFRunLoop / QTBUG-39488), and delivery has no
poll API, so **there is no way to receive on mac** until the platform fix ships. Full evidence +
decision: `../PROJECT_KNOWLEDGE.md` ("macOS/arm64") and issue #4. **Demo on Linux.**

---

## Why this isn't already done

The module-builder (`logos-module-builder`) **does** support Darwin — its `systems` include
`aarch64-darwin`, `common.nix` emits `.dylib`, `mkExternalLib.nix` runs `install_name_tool -id
@rpath/...`, and `mkStandaloneApp.nix` looks for `darwin-arm64` variants. So the toolchain is ready.
We can't build it on the linux dev box because **Nix can't cross-compile Linux→Darwin** — a mac binary
needs a real macOS host. That's you.

---

## Assumptions to test (in priority order — each is a finding to report)

1. **PRIMARY — does `delivery_module` build for `aarch64-darwin`?** It pulls **zerokit/RLN (Rust + zk
   circuits)**. This is the exact dependency that made `logos-zone-sequencer-module` "linux-amd64 only".
   If zerokit fails to build on darwin-arm64, that's the blocker — capture the exact error.
2. **Does `nix build .#lgx-portable` emit a `darwin-arm64` variant** with bundled `.dylib`s and correct
   `@rpath`/`@loader_path` install names?
3. **Does the mac Basecamp platform build hit the same `getClient` hang** as linux 295 (#150), or does
   delivery getClient work there? (Determines whether runtime test is even possible.)
4. **Does Tor playback work on mac** (`torsocks ffplay` of a live `.onion` stream)?

---

## Prerequisites

```bash
xcode-select --install                      # Apple Command Line Tools (clang, etc.)
sh <(curl -L https://nixos.org/nix/install) # multi-user Nix
mkdir -p ~/.config/nix && echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
# restart shell / `. /nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh`
brew install tor torsocks ffmpeg            # runtime playback helpers (must be on PATH)
gh auth status                              # you need push access (you'll be invited as a collaborator)
```

## Step 1 — clone

```bash
git clone https://github.com/xAlisher/receiver-basecamp
cd receiver-basecamp
git checkout -b mac-build          # work on a branch; PR back to main
```

## Step 2 — THE build (this is the research)

```bash
# On aarch64-darwin this targets darwin automatically. -L streams logs; tee them for the report.
nix build .#lgx-portable -L 2>&1 | tee /tmp/recv-mac-build.log
```

- **If it fails in zerokit / RLN / a Rust crate** → that's assumption #1, the likely blocker. Copy the
  failing derivation + the error tail into the issue. Note whether it's a vendoring/crates.io fetch
  issue (like delivery's v0.1.2 `zerokit-2.0.2` 403, fixed on `main` — we already pin `main`), a
  missing-binary-cache issue ([delivery#37](https://github.com/logos-co/logos-delivery-module/issues/37)),
  or genuine arch-specific source that doesn't compile on aarch64-darwin.
- **If it succeeds** → `result/` has the `.lgx`. Continue.

Also try the dev variant if portable misbehaves: `nix build .#lgx -L`.

## Step 3 — inspect the artifact

```bash
cp -fL result/*.lgx /tmp/receiver-darwin.lgx
mkdir -p /tmp/recv-x && (cd /tmp/recv-x && unzip -o /tmp/receiver-darwin.lgx >/dev/null && find . -maxdepth 3 | sort)
# expect a variants/darwin-arm64/ dir with receiver_ui_plugin.dylib + bundled .dylibs
otool -L /tmp/recv-x/variants/darwin-arm64/*/*.dylib 2>/dev/null   # check @rpath/@loader_path, not /nix/store
```

## Step 4 — install + runtime test (best-effort; needs a mac Basecamp build)

You need a macOS Basecamp app/build with `delivery_module` present. Install the module with `lgpm`
(same recipe as linux — see README "Quick start", swap the variant to `darwin-arm64`):

```bash
PROF="<mac Basecamp data dir>/Logos/LogosBasecamp"
lgpm --modules-dir "$PROF/modules" --ui-plugins-dir "$PROF/plugins" --allow-unsigned install --file /tmp/receiver-darwin.lgx
printf 'darwin-arm64' > "$PROF/plugins/receiver_ui/variant"
```

Launch Basecamp, open **Receiver**, and watch `/tmp/receiver-diag.log`:
- `initLogos … getClient(delivery_module)` followed by **no return** = the mac platform has the same
  #150 hang → report it (runtime test blocked, but the **build** result still stands).
- discovery green + a station playing = full success on mac. 🎉

Diagnostics notes: ui-host stderr is swallowed
([basecamp#163](https://github.com/logos-co/logos-basecamp/issues/163)); the backend writes
`/tmp/receiver-diag.log`. To find the ui-host process, match by **cmdline args** (it's exec'd via the
dynamic loader, so `comm` is the loader, not the plugin name) — `ps -axo pid,args | grep ui-host | grep receiver_ui`.

## Step 5 — report & deliver

- Post all findings (build pass/fail + zerokit outcome + `otool` output + runtime behavior) in the
  tracking issue.
- If the build succeeded: commit the darwin lgx to `dist/receiver_ui-0.1.0-darwin-arm64.lgx`
  (`git add -f`, the repo ignores `*.lgx`), update the README "Platform / arch support" section, and
  open a PR to `main`.

---

## Key knowledge & links

- **README** [`../README.md`](../README.md) — the 268-only caveat, install recipe, runtime deps.
- **PROJECT_KNOWLEDGE** [`../PROJECT_KNOWLEDGE.md`](../PROJECT_KNOWLEDGE.md) — what works, what does NOT
  fix 295, the consumption shape, diagnostics.
- **BRIEF / DESIGN** [`BRIEF.md`](BRIEF.md), [`DESIGN.md`](DESIGN.md).
- Upstream: platform `getClient`/capability regression **logos-basecamp#150**; consumer write-up + the
  `poll()` wchan capture **logos-delivery-module#31**; can't-bundle-runtime-bins
  **logos-module-builder#114**; zerokit nix fix **logos-delivery-module#49**; missing zerokit cache
  **logos-delivery-module#37**; swallowed ui-host stderr **logos-basecamp#163**.
- Builder Darwin support (read these to understand the variant machinery): in the
  `logos-module-builder` flake — `lib/common.nix` (`systems`, dylib), `lib/mkExternalLib.nix`
  (`install_name_tool`), `lib/mkStandaloneApp.nix` (`darwin-arm64` variant lookup).
- `flake.nix` here pins `delivery_module` to **`main`** (zerokit fix). delivery `metadata.json` version
  is `1.0.0` on every tag.
- Catalog (lgpd): `xAlisher/logos-basecamp-modules` — receiver is a submodule there with a
  `release-receiver-basecamp.yml` workflow (currently linux-amd64 only).
