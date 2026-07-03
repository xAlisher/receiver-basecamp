# receiver-basecamp

A lightweight **listen-only** Logos Basecamp module: discover decentralized radio broadcasts over
**LogosMessaging** (`delivery_module`) and play them over Tor — no hosting, no MediaMTX, no hidden
service. It's a single **`ui_qml` module with a C++ backend** (the `logos-delivery-demo` shape), so
the delivery client lives in the **ui-host** process. Interops with live `radio-basecamp` hosts.

---

## ✅ Platform status — works on v0.2.0 (current)

> **macOS users:** on **macOS** the module uses the **relay architecture** and is **verified working**
> on a current host (cpp-sdk ≥ #68) — see [**macOS (arm64)**](#macos-arm64--verified-working-relay-architecture)
> below. `getClient` works on mac; the blocker there was only a stale host cpp-sdk, long fixed.

**On Linux this module works on the current Basecamp build (`v0.2.0`, verified 2026-07-04):** discovery
starts, the delivery node connects, and the design-system UI renders. The old **268-only** restriction is
**lifted** — the v0.2 platform migration resolved the `getClient("delivery_module")` hang that used to
park pre-v0.2 (295-era) builds. Just run it on a current `v0.2.0` AppImage.

<details><summary>Historical — the pre-v0.2 "268-only" <code>getClient</code> hang (resolved)</summary>

Before v0.2, `getClient("delivery_module")` **hung forever** on the 295-era build (`2cb9985c`) — the
ui-host thread parked in `poll()` waiting for a capability/QRO reply that never arrived. A
delivery-specific platform regression (`getClient("storage_module")` worked), tracked as
**[logos-basecamp#150](https://github.com/logos-co/logos-basecamp/issues/150)** /
**[logos-delivery-module#31](https://github.com/logos-co/logos-delivery-module/issues/31)**. Proof at
the time: same module + byte-identical `delivery_module.so` (`d872f77c`) + same profile → `getClient`
returned in ~1 ms on the 268 build (`ef6dca8b`), hung on 295; only the AppImage differed. The module was
pinned to 268 until the v0.2 bootstrap fix. Kept for archaeology.

</details>

---

## macOS (arm64) — verified working (relay architecture)

**Status (2026-06-12): discovery + Tor playback work end-to-end on macOS**, on a Basecamp host built
with **cpp-sdk ≥ #68** (merged 2026-06-08). The earlier "delivery events never dispatch on mac" wall was
**not** a platform limitation — it was a *stale host cpp-sdk*. cpp-sdk **#68** ("marshal provider events
onto the source thread") makes cross-module `messageReceived` dispatch reliably on macOS. Full
investigation + data: **[#5](https://github.com/xAlisher/receiver-basecamp/issues/5)** and
[`research/delivery-on-mac/journal.md`](research/delivery-on-mac/journal.md).

macOS uses the **relay architecture** (this branch): a `type:core` **`receiver_relay`** receives delivery
in a `logos_host` sidecar (where events dispatch), and a **pure-QML** **`receiver_ui`** polls it via
`logos.callModule`. `.onion` playback routes through a **privoxy** HTTP→SOCKS bridge in front of the
listener Tor — macOS has no working `torsocks` (LD_PRELOAD is SIP-blocked;
[#7](https://github.com/xAlisher/receiver-basecamp/issues/7)).

**Host requirement.** A Basecamp app built from **logos-app `master`** (cpp-sdk ≥ #68). **No released
build qualifies yet** (every release pins cpp-sdk ≤ 2026-04-22), so build one:
`nix build github:logos-co/logos-app#bin-macos-app` and run that `.app`.

**Runtime deps** (on PATH, or via `RADIO_*_BIN`): `nix profile install nixpkgs#tor nixpkgs#ffmpeg nixpkgs#privoxy`
— **not** `torsocks` (unused on mac).

```bash
PROF="$HOME/Library/Application Support/Logos/LogosBasecamp"
for f in receiver_relay receiver_ui; do
  lgpm --modules-dir "$PROF/modules" --ui-plugins-dir "$PROF/plugins" --allow-unsigned \
       install --file "dist/${f}-0.1.0-darwin-arm64.lgx"
done
printf darwin-arm64 > "$PROF/modules/receiver_relay/variant"
printf darwin-arm64 > "$PROF/plugins/receiver_ui/variant"
```

Launch the #68+ app, open **Receiver** in the sidebar → stations appear (~10–15s) → **Play**
(first `.onion` play takes ~10–30s while the listener Tor bootstraps). If the GUI app's PATH lacks
`~/.nix-profile/bin`, pass absolute bins:
`open -n LogosBasecamp.app --env RADIO_PRIVOXY_BIN=$(which privoxy) --env RADIO_FFPLAY_BIN=$(which ffplay) --env RADIO_TOR_BIN=$(which tor)`.

---

## Quick start (cold agent, Linux x86-64)

```bash
# 0. Runtime deps on PATH (playback helpers — see "Runtime dependencies" below)
sudo apt install -y tor torsocks ffmpeg

# 1. Use a current Basecamp AppImage (v0.2.0+ — delivery getClient works since the v0.2 migration).
#    On this machine: ~/logos-basecamp-current.AppImage (-> v0.2.0). The old 268-only pin is lifted;
#    pre-v0.2 (295-era) builds hung getClient — see the "Platform status" section above.

# 2. Use a MINIMAL ISOLATED profile (delivery_module + receiver_ui only) to keep startup fast.
export XDG_DATA_HOME="$HOME/.local/share/Logos-radio-only"
PROF="$XDG_DATA_HOME/Logos/LogosBasecamp"

# 3. Install the LGX (linux-amd64 portable build shipped in dist/).
LGPM=$(command -v lgpm || echo /path/to/lgpm)   # logos-package-manager CLI
"$LGPM" --modules-dir "$PROF/modules" --ui-plugins-dir "$PROF/plugins" \
        --allow-unsigned install --file dist/receiver_ui-0.1.0-linux-amd64.lgx
printf 'linux-amd64' > "$PROF/plugins/receiver_ui/variant"   # select the variant

# 4. Launch (only ONE Basecamp at a time — instances share delivery TCP port 60000).
XDG_RUNTIME_DIR=/run/user/$(id -u) WAYLAND_DISPLAY=wayland-0 \
  nohup "$HOME/logos-basecamp-radio-only.AppImage" >/tmp/recv.log 2>&1 &

# 5. Open the "Receiver" panel in the sidebar → it discovers live stations → tap one to play.
```

`delivery_module` is auto-resolved from `metadata.json` `dependencies` and must be present in
`$PROF/modules/` (it ships with the AppImage; copy it into the isolated profile if missing).

On this machine, `scripts/launch-radio-only.sh` does steps 2+4 against the reserved
`~/logos-basecamp-radio-only.AppImage` (268) + `Logos-radio-only` profile. **Do not** repoint other
Basecamp work at that AppImage/profile — they're reserved for this demo.

## Runtime dependencies (per-OS, on PATH)

Playback spawns helpers resolved via `resolveBin()` (env override → PATH). The module runs its **own
listener tor** (its own `SocksPort`, not the system service) and plays `.onion` via `torsocks ffplay`,
direct URLs via `ffplay`.

| OS | Install |
|----|---------|
| Linux (Debian/Ubuntu) | `sudo apt install -y tor torsocks ffmpeg` |
| macOS (arm64) | `nix profile install nixpkgs#tor nixpkgs#ffmpeg nixpkgs#privoxy` — **privoxy, not torsocks** (`.onion` plays via the privoxy→Tor bridge; torsocks/LD_PRELOAD is SIP-blocked) |

Bundling these inside the `.lgx` isn't fully landing yet — the portable bundler drops extra binaries
([logos-module-builder#114](https://github.com/logos-co/logos-module-builder/issues/114)), so on mac the
helpers are resolved from PATH. Override paths with `RADIO_TOR_BIN` / `RADIO_FFPLAY_BIN` /
`RADIO_PRIVOXY_BIN` (Linux also: `RADIO_TORSOCKS_BIN`).

## What it does

- **Discover** — subscribes to the public directory topic (`/radio-basecamp/1/directory/json`) plus any
  private topics you add; live stations appear and are TTL-pruned (~45s).
- **Listen** — tap a station → Tor-routed playback with a listener jitter buffer (rides out Tor latency).
- **Settings cogwheel** — listener buffer (2–20s) and a **Hide cache** privacy toggle (suppress + clear
  the on-disk stream cache).

## Build from source

```bash
nix build .#lgx-portable   # linux-amd64, $ORIGIN-bundled libs → the installable LGX (in ./result)
nix build .#lgx            # dev variant (Nix-store rpath; needs the same store present)
nix run                    # preview the UI standalone (logos-standalone-app)
```

Notes:
- `delivery_module` is pinned to **`main`** in `flake.nix` (its zerokit/RLN nix build is fixed there,
  [delivery#49](https://github.com/logos-co/logos-delivery-module/issues/49); the v0.1.2 tag's
  `zerokit-2.0.2` vendor step 403s on crates.io). delivery's `metadata.json` version is `1.0.0` on
  every tag — the git pin doesn't change the running version.
- The flake's `src = ./.` is a **git** source: any new file (e.g. the icon) must be `git add`-ed or the
  build can't see it.
- Icon path gotcha: the QML *view* is copied from `src/<viewDir>` (`src/qml/`), but `metadata.json`
  `icon` resolves **repo-root-relative** (`src + icon`) — so the icon lives at `qml/receiver.png`
  (top-level), not `src/qml/`.

## Architecture (consuming delivery the way that works)

- Get **only** the delivery client: `getClient("delivery_module")` + `invokeRemoteMethod` — **not** the
  all-modules `LogosModules`, whose eager getClient-over-every-module is worse.
- `setBackend(this)` first in `initLogos` (fast view↔backend handshake), then a **deferred** getClient
  (so on 295 the ui-host stays alive instead of being SIGKILLed at the 2 s handshake timeout — see
  Diagnostics).
- Pre-seed the capability token via `TokenManager` (the stash↔storage workaround). It does **not** fix
  295, but it's the correct bootstrap on builds where getClient works.
- See [`docs/BRIEF.md`](docs/BRIEF.md), [`docs/DESIGN.md`](docs/DESIGN.md), and
  [`PROJECT_KNOWLEDGE.md`](PROJECT_KNOWLEDGE.md) for the full story (incl. things that do *not* fix 295).

## Diagnostics

ui-host child stderr is swallowed ([basecamp#163](https://github.com/logos-co/logos-basecamp/issues/163)),
so the backend writes a timestamped trail to `/tmp/receiver-diag.log`. To capture the hung `getClient`
on 295: use the deferred build (stable hung process) and match the ui-host by its **cmdline args** —
it's exec'd via `ld-linux`, so `comm`/`/proc/pid/exe` point at the loader, not `.ui-host.elf`. A full
gdb backtrace needs `kernel.yama.ptrace_scope=0` (root); the kernel `wchan` needs neither.

## Platform / arch support

- **linux-amd64** — prebuilt LGX: `dist/receiver_ui-0.1.0-linux-amd64.lgx` (direct ui-host consumer;
  **works on the current `v0.2.0` build** — the 268 pin is lifted, see "Platform status" above).
- **macOS/arm64** — prebuilt **relay-architecture** pair: `dist/receiver_relay-0.1.0-darwin-arm64.lgx`
  (core) + `dist/receiver_ui-0.1.0-darwin-arm64.lgx` (pure-QML). **Verified working end-to-end**
  (discovery + `.onion` Tor playback) on a cpp-sdk ≥ #68 host — see the [macOS](#macos-arm64--verified-working-relay-architecture)
  section. Install both with `variant = darwin-arm64`.

## Status & license

Discovery + Tor playback validated **end-to-end** (discovers a live `radio-basecamp` station and plays
it), originally on 268 and now on the current **`v0.2.0`** build — the v0.2 migration resolved the
`getClient` regression (#150) that had pinned it to 268. License: MIT or Apache-2.0.
