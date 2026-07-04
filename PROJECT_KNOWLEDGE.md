# receiver-basecamp — Project Knowledge

Accumulated wisdom. Patterns, pitfalls, proven facts. (Raw captures live in `docs/retro-log.md`.)

---

## Phase 2 roadmap — Identity & Scaling (pointer, 2026-06-23)

The canonical, prioritized plan for the next epic lives in **radio-basecamp**
(`docs/plans/radio-implementation.md` → *Phase 2 — Identity & Scaling roadmap*) — it's cross-module and
anchored on radio's station signing key, so it's tracked there. receiver's three issues in that plan:

- **#13 — verify station identity by pubkey, not name** (P2.1a) — depends on radio#24 (signed announces).
- **#14 — pin a station + background desktop notification when it's live again** (P2.1b) — depends on #13;
  gated on a `trivial-experiment-first` check that Basecamp keeps the relay/core module alive with no panel
  focused (relates to the orphaned-playback lifecycle issues #2/#10).
- **#15 — restream on your own .onion + announce as a mirror** (P2.3a) → **#16 — aggregate endpoints by
  identity + select best, with failover** (P2.3b) — the scaling mesh; depend on radio#25 (signed media
  digests) + radio#24. The BRIEF's "origin uplink is the scaling limit" Phase-2 path.

Order: identity theme (radio#24 → #13 → #14) ships first as the cheap win; scaling mesh (radio#25 → #15 →
#16) follows, with the radio#25 spike pulled forward as the riskiest unknown.

---

## What works (proven end-to-end, 2026-06-11)

receiver_ui (single `ui_qml` module + C++ backend) **works fully on the 268 build** (`ef6dca8b`):
loads, discovers the live Sneg "Logos manifesto" station over `delivery_module` (status pill green,
264-byte announce received over the logos.dev relay), and **plays it over Tor** (`torsocks ffplay`,
PipeWire sink active = audible). The module/architecture is correct.

## macOS/arm64 (2026-06-12): WORKS end-to-end — discovery + .onion Tor playback

**The earlier "CANNOT receive — platform event bug" verdict was WRONG (confounded).** It was measured on
a Basecamp host whose cpp-sdk predated **#68** ("marshal provider events onto the source thread",
2026-06-08). On a host built from logos-app `master` (cpp-sdk ≥ #68), the *same* `type:core` relay
receives `messageReceived` **7/7 (100%)** on macOS. Root cause was a stale host SDK, not the platform.
Full data: receiver-basecamp#5 + `research/delivery-on-mac/journal.md`. (Methodology fail that produced
the wrong verdict: read "#79 symbol absent" from `strings`, which can't see C++ template instantiations —
use `nm | c++filt`.)

Settled findings:

- **zerokit/RLN builds on `aarch64-darwin`**; `nix build .#lgx-portable` emits a `darwin-arm64` variant.
- **`getClient` + request/reply work on mac** (`createNode`/`start`/`subscribe`/`send`/`getNodeInfo`).
- **✅ Cross-module EVENTS dispatch on mac with cpp-sdk ≥ #68.** The fix lives in the **host** binary
  (`logos_host`/`ui-host` runs the source-side ModuleProxy). **No released Basecamp ships ≥#68 yet** (all
  pin cpp-sdk ≤ 2026-04-22) → build `nix build github:logos-co/logos-app#bin-macos-app`. Verify a host has
  it: `nm logos_host | c++filt | grep -c runOnOwnerThread` (>0). CFRunLoop/QTBUG-39488 is **rejected** for
  the core path (core sidecars are headless, kqueue dispatcher). (basecamp-skills
  `darwin-delivery-events-need-cpp-sdk-68`; the old `darwin-cross-module-event-ipc-broken` is deprecated.)
- **.onion playback on mac uses a privoxy HTTP→SOCKS bridge, NOT torsocks** (LD_PRELOAD is SIP-blocked).
  `#ifdef __APPLE__` in the relay spawns privoxy (forward-socks5t → listener tor SOCKS) + ffplay
  `-http_proxy`. Linux keeps `torsocks ffplay`. (basecamp-skills `darwin-onion-playback-privoxy-bridge`; #7.)
- **Build with `.#lgx-portable`, never `.#lgx`** (dev variant silently won't load — `darwin-lgx-portable-required`).
- **Profile core modules load on demand** (only when a consumer/ui-dep pulls them — `darwin-core-module-on-demand-load`).
- **delivery `createNode` is async** now: it returns an empty `LogosResult` immediately and signals
  success via callback. The relay does NOT gate on the result (just proceeds to `subscribe`) — correct;
  the workshop `voting` module gates on a sync success bool and bails (`deliveryStatus=3`). Stale-consumer hazard.
- **delivery payload encoding drift:** on the #79 host `messageReceived` `data[2]` arrives as **raw**
  announce JSON (base64 on linux-268). `ingestAnnounce` now parses JSON-first, base64-fallback (commit 6b5e907).

**Architecture on mac:** the **relay arch** (this branch) — `receiver_relay` (core, receives in
logos_host) + pure-QML `receiver_ui` (polls via `logos.callModule`). Shipped lgxs in `dist/`. Once a
≥#68 GUI app is broadly available, the direct ui_qml C++ backend (top-level, `main`'s design) should also
work — confirm the ui-host CFRunLoop path then (the one cell not measured headlessly).

## ✅ RESOLVED (v0.2.0, 2026-07-04): the getClient hang is gone — receiver works on the current build

The v0.2 platform migration fixed the getClient/capability bootstrap (logos-basecamp#150). On the
current **`v0.2.0`** AppImage the DIRECT ui_qml consumer runs end-to-end: discovery starts, the delivery
node connects (verified `connectionStateChanged` transitions `Disconnected→Connected` in the run log),
and the design-system UI renders. **The 268-only pin is lifted.** The section below is pre-v0.2 history;
the *diagnosis method* (controlled AppImage swap + file-diag the blocking call) is the reusable part.

## The blocker: getClient("delivery_module") hangs on 295 — a PLATFORM regression (pre-v0.2, RESOLVED)

On the pre-v0.2 pre-release (`2cb9985c`/295), `getClient("delivery_module")` **blocked forever** (ui-host
stays alive, event loop frozen → view handshake times out → permanent spinner / "stuck initializing").
The identical call returns in **1 ms on 268**. Isolated by controlled comparison — same
`delivery_module.so` (byte-identical `d872f77c`), same receiver, same minimal profile, same machine;
**only the AppImage differs**. So it's the platform's getClient/capability/QRO layer, NOT receiver,
delivery, profile, token, or version. `getClient("storage_module")` works on 295 (stash) → delivery-
specific. Full skill: `basecamp-skills/delivery-getclient-hang-295`. (Exact line: pending gdb → upstream.)

### Things that do NOT fix it on 295 (don't re-try)
- init timing: synchronous-in-initLogos (→ blocks handshake → spinner) **or** deferred (→ event loop
  frozen mid-getClient → no discovery). Both are the same hang at a different moment.
- pre-seeding the capability token via `TokenManager::instance().saveToken("delivery_module", …)`
  (stash↔storage workaround) — applied, getClient still hangs.
- minimal isolated profile (only delivery + receiver) — still hangs (so it is NOT QRO-allocator
  degradation from a busy profile, the `ipc-client-eager-init` "permanent failure mode").
- matching the delivery version — moot: delivery **metadata version is `1.0.0` on every git tag**
  (v0.1.1/v0.1.2/main); the tag pin doesn't change the running version.

## Consumption shape (the correct one)

- Get **only** the delivery client: `m_delivery = logosAPI->getClient("delivery_module")` +
  `invokeRemoteMethod("delivery_module", "createNode"/"start"/"subscribe", …)` + `requestObject` +
  `onEvent("messageReceived", …)`. Do **not** construct the all-modules typed `LogosModules` — its
  eager getClient over *every* module in the profile hangs on one module's capability handshake.
- `setBackend(this)` FIRST in initLogos (view source ready → fast handshake), then the delivery work.
- `delivery_module` pinned to **main** in `flake.nix` (the zerokit/RLN nix build fix, delivery #49,
  unblocks the v0.1.2 `zerokit-2.0.2` crates.io-403 that forced an earlier v0.1.1 pin).

## Universal API migration (#20) — WORKS via fire-and-forget async (branch `feat/universal-api-migration`)

The legacy `getClient`/`invokeRemoteMethod` shape above still ships on `main`. The universal port
(`interface:universal`, `receiver_ui_backend.*` deriving `ReceiverUiSimpleSource, LogosUiPluginContext`,
`modules().delivery_module.*` + `.on(...)`) is **proven end-to-end** (headless AND real Basecamp v0.2.0:
discovery + pill + `.onion` audio) — **no upstream/builder/cpp-sdk fix needed**.

- **The trap:** a **synchronous** `modules().delivery_module.createNode()` **deadlocks** — it blocks the
  single-threaded ui-host loop, and delivery's reply is delivered *through* that loop → never returns.
  Compiles clean, hangs at runtime. Diagnosed via `/proc/<ui-host>/wchan = do_wait` (block is in the IPC
  consumer's subprocess wait, *deeper than codegen* — a generator QEventLoop patch was proven insufficient,
  fork-tree Node 7).
- **The fix (in our module):** fire-and-forget `createNodeAsync` → `QTimer 3s` → `startAsync` +
  `subscribeAsync` + wire events. Don't chain on the reply callback (createNode's reply is gated behind
  `start()` → circular). State/data ride the event PUSH (`.on("connectionStateChanged"/"messageReceived")`).
  Skill: `basecamp-skills/universal-modules-sync-call-deadlocks-ui-host`. Full log: `docs/universal-migration-fork-tree.md`.
- **Before merge:** reconcile the design-system QML/version onto the branch; the backend path is done.

## Two Main.qml — which one ships (READ before editing the view)

This repo carries **two** views for two architectures. Editing the wrong one = zero visible effect.
- **`src/qml/Main.qml` — DIRECT backend, THE ONE THAT BUILDS.** `metadata.json` `view: "qml/Main.qml"`
  (src-layout) → `nix build .#lgx` / `nix run .` bundle this. Binds a C++ QRO backend via
  `logos.module("receiver_ui")` (PROPs auto-sync, SLOTs direct, SIGNALs via `Connections`). Backend =
  `src/receiver_ui_plugin.cpp`, interface = `src/receiver_ui.rep`. **The only supported shape**
  (delivery-core-consume-crash).
- **`receiver_ui/Main.qml` — RELAY variant, mac-only.** Pure-QML driving a `type:core` `receiver_relay`
  via `logos.callModule` (feat/mac-core-relay). Built by no build file on feat/design-system-ui.

Branch keeps both in sync (develop on relay → port to DIRECT). Skill:
`basecamp-skills/verify-which-main-qml-the-build-bundles`. Verify with
`grep view metadata.json` + reading the Main.qml inside the built plugin dir.

## Design-system UI (#17) + live connection pill (2026-07-04)

- **Full logos-design-system adoption** in `src/qml/Main.qml`: LogosText/LogosButton/LogosBadge/
  LogosSlider/LogosSwitch/LogosTextField + `Theme.typography`/`Theme.spacing` tokens, zero raw hex.
  RC3+ Basecamp provides the design system natively — just `import Logos.Theme` / `Logos.Controls`,
  bundle nothing. `nix run .` resolves both standalone (its harness spins a real Logos host).
- **Status pill is truthful now.** Old code latched `connectionStatus` at "connecting" / the relay
  latched `m_deliveryNodeUp=true` forever → pill stuck green after startup, could never warn on a drop.
  Fixed: subscribe to delivery_module's **`connectionStateChanged`** event (`data[0]` =
  Connected|PartiallyConnected|Disconnected) and drive the pill live — success/warning/error, delivery-
  demo's logic. Skill: `basecamp-skills/delivery-connection-state-pill`.
- **Op note:** kill Basecamp/standalone by PID (`pgrep -f … | grep -vw $$`), never `pkill -f <str>`
  where the string is in your own command — it self-SIGTERMs (exit 144) and half-kills the host.

## Playback (lifted from radio, works)

`torsocks ffplay` for `.onion`, `ffplay` for direct. The module spawns its **own listener tor**
(`SocksPort`, bootstraps to 100%), then `torsocks ffplay` with `-cookies "cookieCheck=1; path=/"`
(MediaMTX Secure-cookie) + `-infbuf -live_start_index -<buffer>` jitter buffer. Binaries via
`resolveBin` (env → PATH); tor/ffmpeg installed per-OS (option 1; #114 blocks bundling in the lgx).

**"Caching finished but no sound" is almost never the receiver — it's the `.onion` transport.** The
caching countdown is a UI timer only; it flips to "Playing" before audio actually arrives. Diagnose
top-down (playbook: memory `onion-playback-no-sound-playbook`, issue #12): (1) **broadcaster onion
descriptor** — radio host `torhost-*/hs.log` shows `can't upload its current descriptor` = onion is
DARK, no client can find it → `No route to host`; restart the radio station to republish (`status 200`).
(2) **MediaMTX HLS cold** — on-demand without `hlsAlwaysRemux`; a plain `curl index.m3u8` is empty even
when healthy — test with `ffprobe` (`aac 48000 2ch`), keep a warm reader. (3) **receiver's listener Tor
cached the failure** — after the onion returns it keeps failing; `pkill -f receiver_ui/torlisten` → next
tap spawns a clean Tor. Receiver-side fix owed (#12): reap+respawn the listener Tor on failure; reconsider
ffplay `-live_start_index -42`/`-autoexit` (gives up on a cold/short playlist).

## Reserved isolated environment (DO NOT touch from other work)

- `~/logos-basecamp-radio-only.AppImage` is the **268 build (`ef6dca8b`)** — the one where receiver
  works end-to-end (discover + play). (It was briefly a 295 copy; repointed to 268 since 295 hangs
  getClient.) Profile `~/.local/share/Logos-radio-only`; launch `scripts/launch-radio-only.sh`.
  `~/logos-basecamp-khidr.AppImage` is the same 268 (`ef6dca8b`). The latest **295** build is
  `~/logos-basecamp-current.AppImage` (`2cb9985c`) — use it only to reproduce the getClient hang.
  Only ONE Basecamp runs at a time (separate profiles still share delivery TCP port 60000 → conflict).

## Diagnostics

ui-host child stderr is swallowed (basecamp#163) and qInfo is filtered. The backend writes a
timestamped file trail to `/tmp/receiver-diag.log` (`diag()` in `receiver_ui_plugin.cpp`) — this is
how the getClient hang was finally pinned ("getClient(delivery_module)" line with no return). Keep it
until the 295 issue is resolved.
