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

## Resilience + honest play-state epic (2026-07-05) — the receiver side of #12 shipped

**Detect real playback state from ffplay's `-stats` stderr** (the load-bearing fact — measured, don't
re-derive). Spawn ffplay with `-stats`; the status line is `<master clock> M-A: … aq= <N>KB …`:
- **leading field `nan`** = connecting/buffering, **a real number** = audio is ACTUALLY out (the master
  clock only ticks once samples hit the device). Measured transition: `nan → 4395.23` at the first sample.
- **`aq= 0KB`** prints even with zero data (the initial line); **`aq> 0`** = ffplay pulled real stream
  bytes. So `aq>0` ≠ "playing" — it's "buffering/connect-works", one step earlier than the clock.
→ Three honest phases: **connecting** (no bytes) → **caching** (`aq>0`) → **playing** (clock ticks).
Drive the UI off these PROPs, never a countdown timer (the old cache countdown flipped to "Playing" over
silence). `buffering` + `playbackLive` PROPs expose them; `torStatus` polls `Bootstrapped N%` from the
listener tor's own log (off/booting/ready/failed) for the header service badge.

**No-audio watchdog (#23):** ffplay with `-infbuf` **buffers silent instead of exiting** on a stuck Tor
rendezvous, so the exit-based #21 retry never fires. Watchdog: no `aq>0` within a window → **reap the
listener Tor + retry**. Tuning that actually works: window must exceed a fresh Tor's ~25s bootstrap+connect
(35s), and **reap up to 3×** before giving up — one window falsely reports a reachable station "unreachable"
(Tor onion rendezvous is 9s–>55s variable; a live station connects within a retry or two). Message: NOT
"offline" — say "couldn't reach over Tor" (the station is usually live; the onion descriptor went dark).

**Pre-warm the rendezvous does NOT work — don't re-attempt it (#26/#28/#29 → reverted #30).** ffplay owns
the audio out of a separate process, and a warm SOCKS socket to the onion *is* the rendezvous — the same
slow op moved earlier, so it only helps if it completes before Play, and it hangs (connecting) without
erroring so retry never fires. The only reliable win kept: spawn the listener Tor early on discovery/hover
(saves the ~11s bootstrap). Fast is owned by the multi-reap recovery, not a pre-build.

**Multi-instance footgun:** `/tmp/receiver_ui/torlisten-*` is **NOT XDG-isolated** — shared across every
Basecamp instance. Running two isolated instances → their Tor reaps fight in the shared dir, and a play
"never resolves". Symptom looked like a receiver bug; was instance interference. Reap orphaned playback by
**PPID=1** (parent ui-host dead), or scope to your own instance.

**Self-match-safe reap command** (shown in Settings, #35): `pkill -f 'receiver_ui/torliste[n]|ffplay.*cookieChe[c]k'`.
The `[n]`/`[c]` bracket char-classes match the real strings (`torlisten`, `cookieCheck`) but keep the
pattern from matching the pkill's own command line (classic `grep '[s]shd'` trick). `cookieCheck` is unique
to the receiver's ffplay; require `ffplay` before it (a bare `cookieCheck` matched an unrelated grep in test).

## Cross-module now-playing + private topics epic (2026-07-05) — #40, #41, #44 (radio #35, #49)

The listener half of two cross-module features. Both ride the existing announce/heartbeat; the broadcaster
half lives in radio_module/radio_ui (radio PROJECT_KNOWLEDGE).

**Now-playing (#40):** the announce carries an optional `nowPlaying` string (radio reads it from a file
Liquidsoap's `on_metadata` writes). `ingestAnnounce` stores it (sanitized — attacker-controllable, so
`.remove([\x00-\x1F\x7F]).left(120)`), `publishStations` carries it, and the list row + player bar render
`"Playing now: <show>"` (hidden when empty). The row grows to a 3rd line; the player bar looks it up by the
playing station name (`playingNowText()`). **The source, not the chain, is the usual failure:** PSR's
`nowplaying.txt` was empty because the tracks had no ID3 tags → `m["title"]/m["artist"]` blank. Fix was to
TAG the files (`ffmpeg -c copy -metadata`), not a fragile Liquidsoap filename-fallback.

**Private topics (#44):** a private topic REPLACES the view (not additive). `publicTopic` PROP exposes the
directory topic; QML tracks `selectedTopic` ("" = public) → `activeTopic = selectedTopic || publicTopic`;
`stations()` filters to `s.topic === activeTopic`. The topic stays in the input; a **Switch** button submits
(→ becomes **✕** which clears back to public). `addTopic` subscribes; the directory subscription persists
(filter-only switch).

**BUG that shipped an empty list (cost a user round-trip):** the filter `s.topic === activeTopic` hid EVERY
station because the announce payload didn't carry `announceTopic`, so `s.topic` was empty. Fixed by having
radio's `buildAnnouncePayload` include `announceTopic`. **Canonical alternative we should have used:** the
`messageReceived` event already delivers the source topic at `data[1]` — the receiver's handler blindly
ingests all of `d` instead of reading `data[1]` as the topic (skill: `delivery-module-messaging` → "Filter
announces by content topic"). Follow-up: switch the handler to `data[1]` and the announce field becomes
redundant. **Lesson:** don't filter by a payload field you haven't confirmed the sender emits.

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

## DS adoption + private-topic (retro 2026-07-06)
- **Verify the payload field exists before filtering on it.** Shipped a list filter on `s.topic ===
  activeTopic` before confirming the announce carried `topic`; it didn't → the station list went empty.
  The source topic was ALREADY in the `messageReceived` event at `data[1]` (documented in
  `delivery-module-messaging`). Lesson: read the existing announce/event schema (and the `_index` recipe)
  before building a filter on an assumed field.
- **A derived field doesn't re-derive on auto-resume.** Flipping `visibility=private` didn't move PSR off
  the general topic — auto-resume reads `announceTopic` **verbatim** from `station.json`. Set the concrete
  `announceTopic`, don't rely on re-derivation from `visibility`.
- **Compound asks: track every sub-part.** Surfaced the private topic but missed the "name it" input the
  same request asked for. Re-hit by the user. Enumerate sub-asks of a compound request before closing.
- **Recurring meta:** the stumbles came from not consulting `_index`/the recipe first (`data[1]`=topic,
  `onViewModuleReadyChanged` reactive gate, delivery-demo DS components were all documented). The skills
  worked — the gap was the lookup. Read the index sheet before choosing a fix.

## Verify the RIGHT nix out-link (retro 2026-07-06, #52)
`nix build .#lgx-portable` writes the default `./result`. The `/release` skill uses
`--out-link result-lgx-portable`, which persists as a STALE symlink to an older build. After a plain
`nix build`, verifying `result-lgx-portable/*.lgx` checks the OLD artifact — the symbol/QML looked missing
though the build was fine. Always `readlink ./result` (or pass `--out-link`) and extract THAT `.lgx` before
claiming a change shipped. (`strings` also misses Qt `QStringLiteral` — they're UTF-16; use `strings -el`.)

## Dependency-preflight card (retro 2026-07-06, #55)
Added a first-launch card that detects `tor`/`ffplay` + `torsocks`(Linux)/`privoxy`(mac) via
`QStandardPaths::findExecutable` (honors `RECEIVER_*_BIN` + absolute-path overrides) and shows a
copy-able `apt`/`brew` install command + Re-check. Backend: `depsJson` PROP + `checkDeps()` SLOT;
`publishDeps()` at ctor **and** `onContextReady` (constructor-time PROP set can precede QRO remoting).

- **FAIL — the card didn't render for two rebuild/relaunch cycles.** Root cause: a catalog/lgpd-installed
  **ui_qml** module lives in `~/.local/share/Logos/LogosBasecamp/plugins/receiver_ui/`, which **shadows**
  `modules/receiver_ui/`. My dev build was going into `modules/`; the platform kept loading the old
  `plugins/` copy. The user spotted it ("installed from connected repo — can it be the root cause?").
  Fix: overwrite `plugins/<name>/`. Wrong action: trusted `dev-install-convention`'s "modules/ is the only
  runtime dir" (true for core, WRONG for ui_qml) instead of `module-vs-plugin-terminology` which had the
  correct mapping. Now corrected in the skill. (Confirm which loads: grep the bundled `qml/Main.qml` +
  `strings` the `.so` for a new symbol.)
- **FAIL — `pkill -f "logos_host"` kept killing my own shell (exit 144), 3×.** `pkill -f` matches the
  running command's own cmdline. Kill by numeric PID (`ps -eo pid,comm | awk '/logos-basecamp/'`).
  → skill `pkill-f-matches-own-shell`.
- **WIN — a `diag()` dump of the published `depsJson` cleanly split backend-vs-QML.** It proved the backend
  emitted `ok:false` with the right payload, narrowing the bug to the render/install path (the plugins/ dir),
  not the detection logic. ui-host stderr is swallowed (#163) so the file-diag trail is the only signal.
- **WIN — dogfood loop:** removed `ffmpeg`+`torsocks` (both `apt remove` cleanly, no cascade — verify with
  `apt-get -s remove` first), saw the card, reinstalled, Re-check cleared it live. macOS path untested → #56.

## #65 dependency-OVERLAY gate + v0.2.6 SemVer re-cut (retro 2026-07-22)
Redesigned the #55 preflight card into a **full-panel overlay gate** (scrim + centered `gateCard`, `z:1` so
its buttons sit above the scrim's click-blocking MouseArea) so users can't skip it and reach the stations.
One **self-healing command** (`macFastPath`: install Homebrew if missing → tor/ffmpeg/privoxy → `launchctl setenv`),
a filled-orange **Copy commands** button, then an **"I installed dependencies" → fully quit & reopen** flow.
Shipped as **v0.2.6** (both platforms, signed). Iterated the QML entirely by hot-swap (no `.lgx` rebuild per
tweak) → skill `qml-hot-swap-installed-plugin`.

- **Design system ships only an OUTLINE `LogosButton`.** For a filled/primary CTA, roll your own Rectangle
  (`Theme.palette.primary` = orange300; `Qt.darker(accent,1.15/1.35)` for hover/press — theme-safe, no reliance
  on `primaryHover`/`primaryPressed` existing in both themes) with a `property bool filled` for outline↔filled.
- **FAIL — the "I installed" button appeared dead.** First-guess was the scrim MouseArea eating clicks (added
  `z:1` defensively). Real cause was downstream (a stale deploy, below) — but the `z:1` on the centered card
  above a full-panel click-blocker is correct and worth keeping. Lesson: for an overlay, give the card explicit z.
- **FAIL — deployed a STALE `Main.qml` for a whole round-trip.** In an `ssh '…'` (single-quoted) heredoc, a
  **local** `$INST` var didn't expand on the remote → `cp` wrote to `$HOME/` instead of the plugin path; the
  install looked done but ran old code ("button doesn't work"). Root cause: mixed local/remote var expansion in
  an ssh one-liner. Fix: define remote paths ON the remote (`DEST="$HOME/…"` inside the heredoc) and **always
  `md5` the deployed file == source**. → `qml-hot-swap-installed-plugin`.
- **FAIL — the card's command hung the user's Terminal at `quote>`.** The command had `#` comment lines and
  `# … don't …` apostrophes; macOS zsh (interactive_comments OFF) read the `'` as an open quote. Fix: comment/
  apostrophe-free command, explanation in the description text; command also made selectable (read-only
  `TextEdit`). → skill `qml-copy-command-zsh-safe`.
- **FAIL — shipped v0.2.0.6 (4-part), built+signed BOTH platforms, then the catalog rejected it.** The catalog
  CI's `lgx verify` enforces SemVer 2.0.0; 4-part is invalid AND the stale 4-part `0.2.0.5` poisoned the whole
  index rebuild until unpublished. Re-cut → **0.2.6**. The receiver `0.2.0.x` scheme is dead; use 3-part SemVer,
  encode BC-compat via `+basecampX.Y.Z` (dlipicar confirmed). → skill `module-version-convention` (flipped),
  `/release` skill gained a §0 SemVer gate.
- **WIN — stopped at the SemVer wall and got the version-scheme decision from Alisher** instead of unilaterally
  re-versioning or hacking the shared catalog CI. The `module-version-convention` skill's own pre-written Caveat
  had predicted this exact failure and named the fix (`0.2.<n>`).

## #90/#94/#96 fleet-migration outage + the v0.4.0 regression (retro 2026-08-14)

The longest single debugging session this repo has had. Four distinct faults stacked, and the reason it
took hours was **observability**, not difficulty — every layer failed silently.

### The outage (external, not our bug)
The `logos.dev` delivery fleet migrated Waku **cluster 2 → 3** (`logos-messaging/logos-delivery#4114`),
rolling from 2026-08-08 23:28 CEST and complete by 08-10 13:45. nwaku's peer_manager hard-drops any peer
whose cluster differs, so all six hardcoded entry nodes were dialled *successfully* and disconnected
milliseconds later (`different clusterId reported: 2 vs 3`) → `totalConnections=0/150`, red pill, on mac
and Linux simultaneously. **DNS and TCP both look perfectly healthy**, which is what makes this confusing:
resolve the hosts, connect to :30303, everything passes — and the node still has zero peers.

Diagnostic order that works: grep the Basecamp log for `different clusterId` *before* suspecting anything
local. Blast radius is every module hardcoding the preset, so broadcaster and listener must move together
or the directory is silently empty.

### The fix, and the wrong first version
`logos.test` is the network upstream guarantees ("logos.dev is subtle to change at any moment" —
logos-co/logos-delivery-module#84) and is on **cluster 2**, the same cluster `logos.dev` selects.

v0.4.0 asked for it **by preset name** and was dead on every stock install: the `delivery_module` the
package manager resolves (v1.1.0) has only `logos.dev` compiled into `liblogosdelivery.so`, so
`createNode` was rejected (`Invalid --preset value passed: logos.test`), no node was created, and the pill
sat amber with **no cluster mismatch to explain it**. v0.4.1 names `logos.dev` and picks the fleet with
explicit `logos.test` `entryNodes` — portable across every delivery_module build. Pinning "a newer
delivery_module" is not a fix and is not even expressible: the resolver's `1.1.0` sorts *newer* than the
`0.2.0` that has the preset.

### Why it was invisible (the real cost)
- A `ui_qml` module's `qInfo`/`log()` reaches **no** Basecamp log — verified, zero `[*_ui]` lines for any
  module — so every early-return in `ingestAnnounce` was silent.
- All receivers appended to one `/tmp/receiver-diag.log`; with three running the trail is unattributable.
- v0.4.2 fixes both: every drop path diag'd with its reason, an `ingest ok` line, a `publishStations`
  summary (count + `publicTopic` + per-station topic, so a *filter* problem is distinguishable from an
  *ingest* problem), and a per-instance diag file. One line then answered it:
  `ingest ok: "Parallel Society Radio" verified=yes topic=/radio-basecamp/1/directory/json`.

### Operational rules learned
- **`createNode` is once per delivery_module PROCESS** (`createNode rejected - context already
  initialized`). Installing a network change into a running Basecamp does nothing — full quit + relaunch
  is mandatory and belongs in every release note.
- **Run one Basecamp at a time.** Two instances with empty `LOGOS_INSTANCE_ID` share the IPC socket
  namespace; you can end up staring at a Receiver that isn't the instance doing discovery. This, not a
  code bug, is what "updated, restarted, still no station" turned out to be.
- **The M1 is SSH-reachable (`m1`, user `sher`) — mac builds are NOT wetware.** `nix build
  .#lgx-portable --impure` there (never `.#lgx`), sign on the Linux box, `lgx merge` the two into one
  multi-variant package for the catalog.
- Deleting an iso tree needs `chmod -R u+w` first — `cp -a` preserves nix-store read-only bits and `rm`
  silently half-fails.

### Process fail worth keeping
v0.4.0 was validated against the `delivery_module` that happened to be on the dev box (v0.2.0, which has
`logos.test`), not against what a stock install resolves. It passed every check here and was broken for
every user. **Validate a delivery config against the module a stock install gets** — copy the real one
into a logoscore harness and assert `createNode` returns success. The identical lesson had already been
learned hours earlier on Sneg (dial peers explicitly rather than trust a preset) and was not carried
across; that is the actual root cause of the regression.
