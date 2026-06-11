# receiver-basecamp — Brief

**A lightweight, cross-platform (Linux + macOS/arm64) Logos Basecamp module that *discovers and
plays* decentralized radio broadcasts — no hosting.** It runs on the **latest** Basecamp build (not
the 268-only pin that `radio-basecamp` is stuck on) and lets Mac users tune into onion broadcasts.

Discovery is over **LogosMessaging** (`delivery_module`); playback is `torsocks ffplay`. There is no
origin, no MediaMTX, no Tor hidden service, no C++ build of any of that — which is exactly why it
can ship where the full `radio-basecamp` host cannot.

---

## Why this exists

`radio-basecamp` is **pinned to one old build** (`pre-release-1dc1c08-268`) and is **linux-amd64-only**.
Both limits come from the same place: its `type: core` module consumes `delivery_module`, and on every
newer build that **crashes at load** (`std::length_error` in `LogosAPI::getClient`). So today there is
no way to listen to a Logos radio station from a current Basecamp, or from a Mac. This is tracked as
[radio-basecamp#18](https://github.com/xAlisher/radio-basecamp/issues/18) (listen-only flavor).

## The unlock (the whole reason this is now buildable)

The crash is **not** "delivery is broken" and **not** fundamentally about the module's `type:` label —
it is about **which process the consuming code runs in**:

- A `type: core` module runs in its **own `logos_host` sidecar**, which **never receives the
  `capability_module` bootstrap token** ([basecamp#150](https://github.com/logos-co/logos-basecamp/issues/150)).
  Without that token, `getClient("delivery_module")` over-reads a length field and segfaults
  ([delivery#31](https://github.com/logos-co/logos-delivery-module/issues/31)).
- Code in the **`ui-host`** process (where `main_ui` and every **`ui_qml` module's C++ backend** live)
  **does** get the bootstrap token. There, `getClient("delivery_module")` works.
  **`logos-delivery-demo` is the living proof** — a `ui_qml` module with a C++ backend that consumes
  delivery on current builds. ([tutorial#67](https://github.com/logos-co/logos-tutorial/issues/67)
  documents this as the supported shape.)

**Therefore:** build receiver-basecamp as a **single `ui_qml` module with a C++ backend** that consumes
`delivery_module` for discovery and runs playback as a subprocess. That sidesteps #31/#150 entirely and
runs on the latest platform. (Radio's `ui-qml-backend` branch *attempted* this but kept delivery in the
**core** sidecar, so it still crashed on 295 — the lesson baked in here is: the delivery client must
live in the **ui_qml backend**, not a core dep.)

## What the reference proves — `logos-delivery-demo` (cloned 2026-06-11)

The canonical shape is no longer theoretical — `logos-delivery-demo` is now cloned at
`~/basecamp/refs/logos-delivery-demo` and is our **working template**. Concrete findings:

- It is a **`ui_qml` module that ships a C++ backend**: `metadata.json` is `type: ui_qml` +
  `dependencies: ["delivery_module"]`, *and* it has a `CMakeLists.txt` (`logos_module(NAME … REP_FILE …
  SOURCES …)`) and `src/*.cpp`. So "ui_qml" and "has C++" are not mutually exclusive — the builder
  (`mkLogosQmlModule`) supports a native backend for a UI module.
- **Mechanism (corrects the first draft):** it consumes delivery via the **typed `LogosModules` SDK** —
  `m_logos = new LogosModules(api)` in `initLogos`, then `m_logos->delivery_module.createNode(cfg)` /
  `.start()` / `.subscribe(topic)` / `.on("messageReceived", …)`. **Not** raw
  `getClient`/`invokeRemoteMethod`. This is clean *and* safe **because it runs in ui-host** — the typed
  SDK is exactly what crashes from a core sidecar, and exactly what works here.
- **QML ↔ backend bridge is a Qt Remote Objects `.rep`**, not `logos.callModule`: `.rep` `PROP`s
  auto-sync to QML (`backend.nodeReady`), `SIGNAL`s arrive via `Connections { target: backend }`, and
  `SLOT`s are called directly (`backend.startDiscovery()`), wrapped in `logos.watch(...)` for async
  results. QML grabs the backend with `readonly property var backend: logos.module("<name>")`.
- **Cross-platform confirmed at the source:** delivery-demo's README states prerequisites "macOS
  (aarch64/x86_64) or Linux (aarch64/x86_64)" and pins `delivery_module` **v0.1.2** — matching our matrix.
- **Build/run path:** `nix build` → `nix run` (standalone UI preview, no full Basecamp needed) →
  `nix build .#lgx` → `lgpm install`. The standalone `nix run` is the fast inner loop for milestone #2.
- **Interop encoding (discovery, M3):** a `radio-basecamp` host `send`s the announce JSON as the payload;
  the receiver reads `messageReceived` `data[2]` and does a **single base64 decode → JSON** (radio's
  proven path). `data` layout: `[0]=hash, [1]=topic, [2]=payload, [3]=timestamp(ns)`.

## Status (2026-06-11)

Repo skeleton initialized at `~/basecamp/modules/receiver-basecamp` (git, licenses, gitignore, docs).
Scaffolding `receiver_ui` from the delivery-demo template; landing **milestone #2** (delivery init in the
ui_qml backend, no crash on latest `295`) plus the **settings cogwheel** (listener-buffer slider + a
"hide cache" privacy control, per radio#19 / the Stash settings-pane pattern). A reference build of
delivery-demo is validating the toolchain and the exact generated class names before the receiver backend
is finalized.

## Cross-platform, for free — verified

Listening needs only delivery (platform IPC) + `tor`/`torsocks`/`ffmpeg` (ship on macOS & arm). Dropping
the host-only deps (MediaMTX, Tor-host) removes the arch-specific binaries that block `radio-basecamp`'s
multi-arch release. So `receiver-basecamp` targets **`linux-x86_64`, `linux-arm64`, `darwin-arm64`** —
exactly the arches the platform already ships.

**Verified 2026-06-11** (the earlier "does delivery exist on Mac/arm?" unknown is now resolved):
- The **latest** Basecamp pre-release (`pre-release-63b35e8-295`) ships **macOS/arm64**
  (`logos-basecamp-aarch64-unsigned.app.tar.gz`) **+ Linux arm64** (`…aarch64.AppImage`) **+ Linux x64**
  (`…x86_64.AppImage`). Stable `0.1.2` even ships a `.dmg`.
- **`delivery_module` is built for macOS**: its `metadata.json` bundles `.dylib` (macOS) and `.so` (Linux)
  native libs (`liblogosdelivery`, `librln`, `libpq`), and its CI matrix builds on
  **`ubuntu-latest` + `macos-latest`** (Apple Silicon). Latest tag: **v0.1.2** (delivery-demo pins this).
- (Windows: delivery ships `.dll` libs, but Basecamp 295 has no Windows asset → out of scope.)

Remaining packaging detail (not a blocker): confirm `delivery_module` is provided per-arch via the
catalog/bundle on a fresh macOS install so the receiver's `dependencies: ["delivery_module"]` resolves —
CI proves it *builds* for mac; this just confirms it's *shipped* there.

## Scope

| In | Out |
|---|---|
| Discover stations over `delivery_module` (public directory topic + private topics) | Hosting / broadcasting anything |
| TTL pruning of dead stations (heartbeat-based) | MediaMTX, Tor hidden service, ingest minting |
| Play a station: `torsocks ffplay` for `.onion`, bare `ffplay` for direct; jitter buffer | Video (audio-first, same as radio v1) |
| Interop with existing `radio-basecamp` hosts (same announce schema + topic) | Any new wire protocol |

## Definition of done (P0)

On a **current** Basecamp on **both Linux and macOS/arm**, open receiver → the live **"Logos manifesto"**
station (broadcast by the Sneg host) appears in the list → tap it → audio plays over Tor. No central
index, no 268 pin.

See [`DESIGN.md`](DESIGN.md) for the architecture, the exact delivery recipe, and the milestone plan.
</content>
