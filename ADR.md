# Architecture Decision Records — Receiver (Logos Radio, listener)

> **App:** Receiver — listen-only Logos Basecamp module (`receiver_ui`, `ui_qml`) + a standalone
> **Android/React-Native** app. Discovers decentralized radio over Logos Messaging and plays it
> over Tor. No hosting, no MediaMTX, no hidden service of its own.
> **Version:** Basecamp module **v0.2.0.5** (Linux + macOS/arm64) · Android **v1.0.0**.
> **Sibling:** [Booth](https://github.com/xAlisher/booth-basecamp) — the broadcaster half.
> **Scope of these ADRs:** the *verify/consume* half of the same discovery + identity contract
> Booth produces. No blockchain anywhere.

---

## ADR-1: Consume `delivery_module` from a `ui_qml` C++ backend, not a `type:core` sidecar

**Decision:** Receiver is a single `ui_qml` module whose C++ backend consumes `delivery_module`
for discovery and runs playback as a subprocess — `type: ui_qml`, `dependencies:
["delivery_module"]`.

**Alternatives considered:** The tutorial "core module + QML UI" split (the shape used by the
broadcaster). Rejected because a `type: core` module runs in its own `logos_host` sidecar that
never receives the `capability_module` bootstrap token, so consuming `delivery_module` from a core
sidecar **crashes at load** (`std::length_error` in `LogosAPI::getClient`) on every recent build.

**Rationale:** Code in the **ui-host** process *does* get the bootstrap token. Building Receiver
as a ui_qml module with a C++ backend is what lets it consume delivery on the latest platform —
and ship on macOS/arm, where the core-sidecar broadcaster cannot.

**Known limitation:** The C++ backend, the delivery client, and the `ffplay` subprocess all share
the single-threaded ui-host event loop — which sets up the deadlock ADR-2 had to solve.

---

## ADR-2: Universal-API migration via fire-and-forget async (root-caused, not worked around)

**Decision:** Discovery fires `createNodeAsync` → (3 s timer) `startAsync` + `subscribeAsync` +
wire delivery events, all fire-and-forget, and then rides delivery's **event push**
(`messageReceived` / `connectionStateChanged`). No synchronous call, no dependence on a reply
being delivered.

**Alternatives considered:** The codegen'd universal wrapper's **synchronous**
`modules().delivery_module.createNode()` — rejected: it blocks the single-threaded ui-host loop
while delivery's reply needs that same loop, so it **deadlocks**. Also rejected after testing:
builder 0.2.0 / master / a thread-safe-IPC SDK branch (all hang or won't compile), and **patching
the generator to run the sync call under a nested `QEventLoop`** — proven insufficient
(`/proc/<ui-host>/wchan = do_wait`: the block is *inside the IPC consumer*, deeper than the
generated wrapper).

**Rationale:** A full red-team fork-tree localized the block to sync IPC on the ui-host loop; the
receiver-side fire-and-forget sequence is the fix that needs no upstream/builder/SDK change. This
migration was proven here first and became the reference the broadcaster later mirrored.

**Known limitation:** The timed 3 s create→start sequence is a heuristic, not reply-gated.

---

## ADR-3: HLS playback via an external `ffplay` subprocess, not Qt Multimedia

**Decision:** `play()` spawns `ffplay` via `QProcess` with `-nodisp -autoexit -loglevel error
-infbuf` and a jitter buffer (`-live_start_index -<bufferSec>`). Binaries resolve via `resolveBin()`
(env override → PATH). `ffplay` is restricted to `http/https` URLs only — never `file:`/`pipe:`/
`concat:`.

**Alternatives considered:** Qt Multimedia / `QMediaPlayer` — rejected: absent from the Basecamp
AppImage runtime, fails silently. QML-sandbox playback — impossible (the sandbox forbids
subprocesses, which is itself a reason the C++ backend is mandatory).

**Rationale:** A subprocess is the only playback path in-sandbox, and ffplay's `-stats` stderr is
what makes the honest state machine (ADR-6) possible.

**Known limitation:** Requires `ffmpeg` on PATH (ADR-9); the `http/https`-only allow-list is a
required security seam, not optional.

---

## ADR-4: `.onion` playback over a module-owned listener Tor, with a per-OS SOCKS transport

**Decision:** Receiver runs its **own listener `tor`** (its own `SocksPort`, not the system
service) and routes playback through it. **Linux:** `torsocks ffplay` (LD_PRELOAD → the listener
SOCKS), pinned with `TORSOCKS_TOR_ADDRESS`/`TORSOCKS_TOR_PORT`. **macOS:** a local **privoxy**
HTTP→SOCKS bridge (`forward-socks5t` → listener tor), with `ffplay -http_proxy` — because torsocks
is unusable under macOS SIP. MediaMTX's cookie gate is pre-supplied (`-cookies "cookieCheck=1;
path=/"`); onion is detected from the URL host (the announce carries no privacy field). The spawn
env is scrubbed of `LD_LIBRARY_PATH`/`LD_PRELOAD` (AppImage poisoning).

**Alternatives considered:** The system Tor service (uses an isolated listener instead); torsocks
on macOS (SIP-blocked → privoxy); **pre-warming the Tor rendezvous** — tried and reverted, it does
not work.

**Rationale:** ffmpeg has no native SOCKS, so the transport has to be shimmed, and the shim differs
by OS. An owned listener keeps Receiver independent of any system Tor.

**Known limitation:** Three per-OS helper binaries (tor, ffmpeg, torsocks|privoxy); the shared
`/tmp/receiver_ui/torlisten-*` dir is not XDG-isolated — a multi-instance footgun.

---

## ADR-5: Verify the station-identity contract on-device; pin by public key

**Decision:** On ingest, a `v:2` announce (secp256k1 pubkey + signature) is verified by
reconstructing the canonical sig-less bytes (`QJsonDocument(...).toJson(Compact)` after removing
`sig`) and checking `StationIdentity::verify(pubkey, sig, canon)` — an invalid signature is a
forgery/tamper and is **dropped**. `v:1` (unsigned) is kept as anonymous/unverified. Verified
stations render `IP hidden by Tor · <3-word PGP fingerprint>` and can be **pinned by pubkey**
(persisted in `QSettings`). Verify is pure/static (secp256k1 ECDSA over SHA-256; 33-byte
compressed pubkey, 64-byte compact sig); the fingerprint is 3 PGP-wordlist words from
`SHA-256(pubkey)`. A `selfTest()` proves keygen→sign→verify and that tamper fails.

**Alternatives considered:** Verifying by station *name* — rejected (impersonable). This module is
the verify half of Booth's signing scheme; the two share a byte-identical canonical-JSON + SHA-256
contract that must stay in lockstep or fingerprints diverge.

**Rationale:** Cryptographic identity means a copied station name cannot impersonate a station, and
pin-by-pubkey follows a specific broadcaster across renames.

**Known limitation:** This is signature verification + pin-by-identity — **not TOFU**. There is no
key-rotation / name-reuse warning: nothing yet alerts the user when a familiar station *name*
reappears under a *different* pubkey. Do not describe it as trust-on-first-use.

---

## ADR-6: Honest playback state from ffplay `-stats`, with a no-audio Tor-reap watchdog

**Decision:** ffplay runs with `-stats`; three phases are derived from its output — **connecting**
(no bytes) → **caching** (`aq>0`) → **playing** (master clock advances off `nan`), exposed as
`buffering` + `playbackLive` properties. A no-audio watchdog: if no `aq>0` appears within ~35 s,
reap the listener Tor and retry, up to 3×. `torStatus` polls `Bootstrapped N%` from the listener
tor log.

**Alternatives considered:** A UI cache countdown (flipped to "Playing" over silence — rejected);
single-reap retry (one window falsely marks a live station dead); rendezvous pre-warm (reverted).

**Rationale:** "Caching finished but no sound" is almost always the `.onion` transport, not the
player — the UI should tell the truth about which, and recover the transport rather than lie.

**Known limitation:** Recovery timing is tuned to Tor's variable rendezvous (~9–55 s).

---

## ADR-7: A QtRO `.rep` backend is the QML↔backend bridge (relay split rejected)

**Decision:** The QML↔C++ boundary is a Qt Remote Objects `.rep` (`ReceiverUi`): properties
auto-sync (`connectionStatus`, `stationsJson`, `pinnedJson`, `depsJson`, …), slots are called
directly (`startDiscovery`, `play`, `pinStation`, `killTorListeners`, `checkDeps`); QML obtains it
via `logos.module("receiver_ui")`.

**Alternatives considered:** Pure QML calling `logos.callModule("delivery_module", …)` — rejected:
the QML bridge is request/response, so receiving the async `messageReceived` stream in pure QML is
unreliable, and playback needs a subprocess QML can't spawn. A **relay split** (a `type:core`
`receiver_relay` + pure-QML UI) built when cross-module events were *thought* broken on macOS —
rejected once that verdict proved to be a confound (a stale host SDK); the universal migration
(ADR-2) collapsed the relay into a single QtRO-backed `receiver_ui`.

**Rationale:** One backend process owns the event stream and the subprocess; QML stays a thin,
compile-free view.

**Known limitation:** The superseded relay sources and an old `Main.qml` still ship in-tree — a
"which one ships" footgun to prune.

---

## ADR-8: Android is a separate native app — embedded Logos Messaging node + kmp-tor, not a Basecamp module

**Decision:** The Android listener is a **React Native** app (RN 0.86 / TypeScript / Hermes) that
runs the **phone's own embedded Logos Messaging (Waku) node** via `liblogosdelivery.so` through a
**JNI bridge** — the first Logos Messaging node compiled for Android — joining cluster 2
(`/waku/2/rs/2/2`), the same directory topic and bootstrap peers as desktop. Signature verify is
JS (`@noble/curves` + `@noble/hashes`, with a ported canonical serialization + PGP wordlist).
Playback is `react-native-video` over an OkHttp `.onion` data source routed through **embedded Tor
(kmp-tor)** (audio only; video hidden).

**Alternatives considered:** A **REST bridge to a desktop node** (recorded in the plan as the
choice because js-waku isn't RN-compatible and go-waku is sunset) — the shipped app keeps REST only
as a *fallback* and makes the **embedded native node primary** (the plan doc is stale on this
point; the code is ground truth). Host-tor-over-adb was the dev path; kmp-tor is production.

**Rationale:** An on-device node makes the phone a fully server-less P2P participant, interoperable
with desktop on the same topics, schema and fingerprints.

**Known limitation:** A separate codebase and a *second* crypto/verify implementation (JS) that
must stay byte-compatible with the C++ one. Released build is **v1.0.0** (debug-signed, sideload,
Android 13+; `1.0.1` is an untagged working-tree bump).

---

## ADR-9: Playback helpers are not bundled — resolved from PATH with a preflight deps card

**Decision:** `tor`, `ffmpeg`/`ffplay`, and the SOCKS shim (`torsocks` on Linux, `privoxy` on
macOS) are runtime deps the user installs; `resolveBin()` resolves env override → PATH. A
first-launch **dependency-preflight card** detects them (`QStandardPaths::findExecutable`), shows a
copy-able `apt`/`brew`/`nix` install command, and offers Re-check (`depsJson` property,
`checkDeps()` slot). `secp256k1` *is* a bundled build/link dep.

**Alternatives considered:** Bundling the helpers inside the `.lgx` — blocked upstream by
`logos-module-builder#114` (the portable bundler drops extra binaries). `$(which …)` in docs —
can print a stale path.

**Rationale:** Resolving from PATH keeps the module arch-independent (the same reason it ships on
macOS where an arch-bundled broadcaster cannot); the preflight turns a silent missing-binary into
an actionable prompt.

**Known limitation:** macOS GUI apps get a minimal PATH, so `launchctl setenv` is sometimes needed
for the card to see installed tools — an open friction point (#58/#59). Portable `.lgx` uses
`$ORIGIN` rpath so dlopen stays under the 2 s token-handshake timeout.
