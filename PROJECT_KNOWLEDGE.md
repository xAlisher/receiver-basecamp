# receiver-basecamp — Project Knowledge

Accumulated wisdom. Patterns, pitfalls, proven facts. (Raw captures live in `docs/retro-log.md`.)

---

## What works (proven end-to-end, 2026-06-11)

receiver_ui (single `ui_qml` module + C++ backend) **works fully on the 268 build** (`ef6dca8b`):
loads, discovers the live Sneg "Logos manifesto" station over `delivery_module` (status pill green,
264-byte announce received over the logos.dev relay), and **plays it over Tor** (`torsocks ffplay`,
PipeWire sink active = audible). The module/architecture is correct.

## macOS/arm64 (2026-06-12): builds + loads, but CANNOT receive — platform event bug

Settled findings from the mac demo attempt (mac LogosBasecamp.app, Diana's host):

- **zerokit/RLN builds on `aarch64-darwin`** — the gating build unknown (#1) is resolved. `delivery_module`
  pinned to `main` builds; `nix build .#lgx-portable` emits a `darwin-arm64` variant.
- **`getClient` works on the mac platform** — no #150 hang here. The node peers, `createNode`/`start`/
  `subscribe`/`send`/`getNodeInfo` all execute. Request/reply IPC is healthy.
- **❌ Cross-module EVENTS never dispatch on mac.** `delivery_module` emits `messageReceived` (waku
  healthy, 16–49 emits/run) but the consumer callback fires **0 times** — proven for BOTH a ui-host C++
  backend AND a `type:core` relay in logos_host. The break is the `QRemoteObjectReplica` event delivery
  over IPC (CFRunLoop / QTBUG-39488), not the host. **The core-relay workaround does NOT help.** delivery
  has no poll API → no API-level receive path on mac until the platform/cpp-sdk fix ships. See
  receiver-basecamp#4 (full evidence) and basecamp-skills `darwin-cross-module-event-ipc-broken`.
- **Build C++ modules with `.#lgx-portable`, never `.#lgx`** — the dev variant `darwin-arm64-dev` ships
  the plugin dylib only (no bundled boost/ssl, `/nix/store` linkage) and **silently won't load**. Portable
  (`darwin-arm64`) bundles them and loads. (basecamp-skills `darwin-lgx-portable-required`.)
- **Profile core modules load on demand** — a `type:core` module installed alone never constructs; it
  loads when a consumer `getClient`s it or a ui module lists it as a dependency. Can't test one in
  isolation. (basecamp-skills `darwin-core-module-on-demand-load`.)

**Decision:** demo on Linux (event dispatch works there — proven on 268). The mac relay
(`feat/mac-core-relay`) is a throwaway; revert to the normal ui_qml event consumer once the platform
fix lands in an AppImage.

## The blocker: getClient("delivery_module") hangs on 295 — a PLATFORM regression

On the current pre-release (`2cb9985c`/295), `getClient("delivery_module")` **blocks forever** (ui-host
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

## Playback (lifted from radio, works)

`torsocks ffplay` for `.onion`, `ffplay` for direct. The module spawns its **own listener tor**
(`SocksPort`, bootstraps to 100%), then `torsocks ffplay` with `-cookies "cookieCheck=1; path=/"`
(MediaMTX Secure-cookie) + `-infbuf -live_start_index -<buffer>` jitter buffer. Binaries via
`resolveBin` (env → PATH); tor/ffmpeg installed per-OS (option 1; #114 blocks bundling in the lgx).

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
