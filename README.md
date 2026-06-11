# receiver-basecamp

A lightweight **listen-only** Logos Basecamp module: discover decentralized radio broadcasts over
**LogosMessaging** (`delivery_module`) and play them over Tor — no hosting, no MediaMTX, no hidden
service. It's a single **`ui_qml` module with a C++ backend** (the `logos-delivery-demo` shape), so
the delivery client lives in the **ui-host** process. Interops with live `radio-basecamp` hosts.

---

## ⚠️ READ THIS FIRST — which platform build to run

**This module only *functions* on the 268-era Basecamp build (`ef6dca8b`). It does NOT work on the
current/295 build (`2cb9985c`).**

On 295, `getClient("delivery_module")` **hangs forever** (the ui-host thread parks in `poll()` waiting
for a capability/QRO reply that never arrives) — a **platform regression**, not a bug in this module
or in `delivery_module` (the delivery binary is byte-identical on both builds and works on 268).
Symptom on 295: the panel loads but discovery never starts ("initializing" / no stations).

- Tracked upstream: **[logos-basecamp#150](https://github.com/logos-co/logos-basecamp/issues/150)**
  (platform root — third-party `getClient`/capability bootstrap) and
  **[logos-delivery-module#31](https://github.com/logos-co/logos-delivery-module/issues/31)**
  (consumer-side write-up + the `wchan` capture).
- Proof: same module + **byte-identical `delivery_module.so` (`d872f77c`)** + same minimal profile →
  `getClient` returns in ~1 ms on 268, hangs on 295. Only the AppImage differs.
- `getClient("storage_module")` works on 295, so it is delivery-specific.

➡️ **To actually use receiver, run it on a 268 AppImage.** When the platform regression is fixed
(#150), it will work on later builds unchanged.

---

## Quick start (cold agent, Linux x86-64)

```bash
# 0. Runtime deps on PATH (playback helpers — see "Runtime dependencies" below)
sudo apt install -y tor torsocks ffmpeg

# 1. Get a 268 Basecamp AppImage (the build where delivery getClient works).
#    On this machine it's already at ~/logos-basecamp-radio-only.AppImage (== ef6dca8b).
#    sha256 must start ef6dca8b. The latest ~/logos-basecamp-current.AppImage (2cb9985c/295) will NOT work.

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
| macOS | `brew install tor torsocks ffmpeg` |

Bundling these inside the `.lgx` isn't possible yet — the portable bundler drops extra binaries
([logos-module-builder#114](https://github.com/logos-co/logos-module-builder/issues/114)). Override
paths with `RECEIVER_TOR_BIN` / `RECEIVER_TORSOCKS_BIN` / `RECEIVER_FFPLAY_BIN`.

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

- **linux-amd64** — provided as a prebuilt LGX in `dist/`.
- **macOS/arm64** — supported by the code, but no prebuilt LGX yet; build from source with `nix`.
- Either way, the **268 platform caveat above applies** until #150 is fixed.

## Status & license

Discovery + Tor playback validated **end-to-end on 268** (discovers a live `radio-basecamp` station and
plays it). Blocked on 295 solely by the platform `getClient` regression (#150). License: MIT or Apache-2.0.
