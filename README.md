# receiver-basecamp

A lightweight, cross-platform **listen-only** Logos Basecamp module: discover decentralized radio
broadcasts over **LogosMessaging** (`delivery_module`) and play them — no hosting, no MediaMTX, no
Tor hidden service. Runs on the **latest** Basecamp (Linux **and** macOS/arm64), unlike
`radio-basecamp` (pinned to one old build, linux-amd64 only).

> **Why it works on the latest platform:** it is a single **`ui_qml` module with a C++ backend**
> (the `logos-delivery-demo` shape). The delivery client lives in the **ui-host** process, which holds
> the capability-token bootstrap — so `LogosModules`/`getClient("delivery_module")` works, instead of
> crashing as it does from a `type: core` sidecar (delivery#31 / basecamp#150). See
> [`docs/BRIEF.md`](docs/BRIEF.md) and [`docs/DESIGN.md`](docs/DESIGN.md).

## What it does

- **Discover** — subscribes to the public directory topic (`/radio-basecamp/1/directory/json`) and any
  private topics you add; live stations appear, TTL-pruned at 45s.
- **Listen** — tap a station → `torsocks ffplay` (`.onion`) or `ffplay` (direct), with a listener jitter
  buffer. Interops with live `radio-basecamp` hosts.
- **Settings cogwheel** — listener buffer (2–20s) and a **Hide cache** privacy toggle (suppress + clear
  on-disk stream cache).

## Runtime dependencies (per-OS, on PATH)

Playback spawns helper binaries resolved via `resolveBin()` (env override → `<module-dir>/bin` →
**PATH**). The module manages its **own listener tor** (its own `SocksPort`, not the system service)
and plays `.onion` via `torsocks ffplay`. So the binaries just need to be installed:

| OS | Install |
|----|---------|
| Linux (Debian/Ubuntu) | `sudo apt install -y tor torsocks ffmpeg` |
| macOS | `brew install tor torsocks ffmpeg` |

Bundling these *inside* the `.lgx` isn't possible yet — the portable bundler drops extra binaries
([logos-module-builder#114](https://github.com/logos-co/logos-module-builder/issues/114)). Once a
per-variant `runtimeBins` mechanism lands upstream, receiver can ship them self-contained per arch
(incl. macOS-native). Until then: PATH + the one-line install above. Override paths with
`RECEIVER_TOR_BIN` / `RECEIVER_TORSOCKS_BIN` / `RECEIVER_FFPLAY_BIN` if needed.

## Build / run

```bash
nix build            # build the plugin
nix run              # preview the UI standalone (logos-standalone-app)
nix build .#lgx      # package an installable .lgx
```

`delivery_module` is pinned to **v0.1.1** (its zerokit builds locally; v0.1.2's zerokit-2.0.2 vendor
step 403s on crates.io). The createNode/start/subscribe/messageReceived API is stable across both.

## Status

Milestone #2 (delivery init in the ui_qml backend, no crash on latest) — in progress. Listener
playback + discovery lifted from `radio-basecamp`'s proven listener half. License: MIT or Apache-2.0.
