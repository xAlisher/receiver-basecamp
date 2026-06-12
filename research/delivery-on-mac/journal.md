# Research journal — delivery on macOS (cross-module event dispatch)

**Investigator:** Sina · **Opened:** 2026-06-12 · **Parent issue:** receiver-basecamp#4
**Question (Alisher):** Other modules (hackyguru + logos-co) use `delivery_module` and *claim* to work.
Build/run them on mac with debugging — do they actually receive? Document their API model vs their
claims. If broken → file issues to them. If working → learn from it and embed in our module.

State tags: `[CODE — X%]` source-confirmed · `[H — X%]` hypothesis · `[CONFIRMED]` measured ·
`[REJECTED]` · `[INCONCLUSIVE — reason]` · `[? — unknown]` needs external data.

---

## Phase 0 — Framing (source survey, no device yet)

### The delivery-consumer landscape (CODE)

| Module | repo | `type` | declares `delivery_module` dep? | receive mechanism | surfaced to UI via |
|---|---|---|---|---|---|
| **logos-delivery-demo** | logos-co | `ui_qml` (+ C++ backend, runs in **ui-host**) | yes | typed SDK: `new LogosModules(api)` → `delivery_module.on("messageReceived", …)` | own `.rep` PROP/SIGNAL (same module) |
| **voting** | hackyguru/logos-workshop | `core` (**logos_host**) | yes | raw: `getClient` → `requestObject` → `onEvent(obj,"messageReceived")` → `handleMessageReceived` | `voting_ui` (`ui_qml`) `logos.callModule` + `listPolls()` poll every 1.5s |
| **tictactoe** (core) | xAlisher fork of hackyguru | `core` (**logos_host**) | **NO** (`dependencies: []`) but calls `getClient("delivery_module")` at runtime | raw: `getClient`→`requestObject`→`onEvent` → `emit eventResponse("remoteMove"/…)` | `tictactoe_ui_cpp` (`type:ui` C++ frontend) |
| **our receiver_relay** | xAlisher/receiver-basecamp `feat/mac-core-relay` | `core` (**logos_host**) | yes | raw: `getClient`→`requestObject`→`onEvent("messageReceived")` (diag at `receiver_relay_plugin.cpp:595`) | pure-QML `receiver_ui` `logos.callModule` |

`[CODE — 95%]` **Two API families, one transport.** Family A = ui-host + typed `LogosModules.on()`
(delivery-demo). Family B = core-sidecar + raw `getClient/requestObject/onEvent` (voting, tictactoe,
our relay). Per #4's bisect, *both* families ultimately ride the **same `QRemoteObjectReplica` event
transport** — the cpp-sdk EventHelper connects to replica signals and forwards "via IPC".

`[CODE — 90%]` voting/tictactoe/our-relay are **byte-for-byte the same receive idiom**
(`getClient`→`requestObject`→`onEvent`). So whatever happens to one core consumer's `messageReceived`
on mac should happen to all three. This is what makes voting a valid proxy for our relay.

### What each module CLAIMS (their docs)

- **workshop §3.11 (voting):** single-instance "echo" proof — the logos.dev fleet echoes your own
  message back through `messageReceived`; the cited proof line is
  `Remote EventHelper: dispatching event "messageReceived" to 1 callback(s) (via IPC)`. **§3.12:** two
  Basecamp instances on **one Mac** (`/Users/guru/Desktop/LogosBasecamp.app`, port override via
  `VOTING_TCPPORT`), "vote from one — the other updates within a second." `[DOC CLAIM]`
- **tictactoe README:** multiplayer is **"experimental"**, only the **C++ `ui`** frontend supports it,
  "Tested with logos-basecamp **v0.1.1** (nicholasgasior AppImage)". Notes a "QML event limitation:
  LogosQmlBridge only exposes callModule()". No explicit mac receive claim. `[DOC CLAIM]`
- **delivery-demo README:** macOS (aarch64/x86_64) listed as a supported prerequisite; "running
  multiple instances on one machine" section exists. Not an explicit "receives on mac" assertion. `[DOC CLAIM]`

### Our data so far (issue #4, prior session, 2026-06-12)

`[CONFIRMED — prior]` On the mac, **both** a ui-host backend (`receiver_ui`) and a logos_host core
module (`receiver_relay`) get **0** `messageReceived` callbacks while `delivery_module` emits the event
16–49×/run. The proof line `dispatching event "messageReceived" … (via IPC)` **never appears**;
EventHelper only ever logs `connected EventHelper … (IPC)`. singleShot(0) deferral did not help.
Conclusion recorded in #4: platform/IPC event-dispatch bug (CFRunLoop/QTBUG-39488 and/or stale
cpp-sdk #68/#79). *Caveat: single run, sender = whatever was live on logos.dev; not a controlled echo.*

### The unresolved contradiction (why this research exists)

workshop §3.11/§3.12 **claim** voting (identical core idiom) receives on a Mac, citing the *exact* log
line our relay never produced. Both cannot be true **on the same build**. Issue #4 never controlled the
**build-version confound** and never built/ran a hackyguru module on the mac to check.

### Environment (CODE)

`[CODE]` mac `/Applications/LogosBasecamp.app` = `co.logos.LogosBasecamp` **0.0.0-dev**, binaries dated
**2026-06-12 01:05** (owner `diana`), `logos_host` + `ui-host` present. Built *after* cpp-sdk #79 merged
(2026-06-08) — but whether it actually bundles #79 is **`[? — unknown]`** (M2).
mac reachable via `ssh mac` (tailscale). Installed modules: `delivery_module`, `receiver_relay`.

### Ranked hypotheses

- **H1 `[H — 65%]`** The workshop's "voting receives on mac" claim was **never verified on the mac
  receive path** for this class of build — it's Linux-verified or aspirational.
  *Falsified if:* building voting on this mac + §3.11 echo test shows `dispatching event
  "messageReceived" (via IPC)` and `handleMessageReceived` fires. Evidence: identical-idiom relay = 0 on
  this build (+confirmed measurement on near-identical code, +15); teaching doc with mac launch scripts
  that may be Linux-authored (−). Guru does run on mac (−).
- **H2 `[H — 60%]`** Cross-module `messageReceived` does **not** dispatch to *any* consumer (core or
  ui-host) on macOS on this build, due to CFRunLoop/CFSocket notifier (QTBUG-39488), **independent of**
  cpp-sdk #68/#79. *Falsified if:* voting OR delivery-demo receives on this mac. *Confirmed if:* both
  also get 0.
- **H3 `[H — 35%]`** The mac dev build does **not** actually include cpp-sdk #79 (pinned older SDK);
  relay failure is the stale-SDK off-thread-drop race; a build with #79 would receive.
  *Test:* M2 (SHA) + M3 (rebuild at #79+).
- **H4 `[H — 20%]`** tictactoe's no-declared-dep (`dependencies: []`) → on-demand getClient changes
  event-dispatch vs declared-dep modules. Lower prior.

### Measurement plan

- **M2** (cheap, do first): cpp-sdk SHA baked in mac app vs #79. Resolves H3 partially.
- **M1** (decisive, cheap-ish): build voting+voting_ui darwin-arm64, install on mac, run §3.11 echo
  **headless** via `logoscore call voting …` (openPoll + vote → fleet echo → messageReceived). Grep
  `dispatching event "messageReceived" (via IPC)` + voting handleMessageReceived. N≥3 runs. Decides H1/H2.
- **M3** (if M1 = 0): control build version — basecamp/cpp-sdk at #79+, retest voting + relay. Resolves H3.
- **M4** (cross-check Family A): build delivery-demo on mac, echo test — does the ui-host typed-SDK path
  receive? Triangulates H2 across families.

### Definition of done

Each of H1–H4 reaches CONFIRMED/REJECTED with logged raw data (grep output, N runs). Outcome #1 = API
survey above (done at CODE level; confirm receive behaviour empirically). Outcome #2 = if a claim is
falsified, draft issue to hackyguru/logos-co. Outcome #3 = if a module receives where ours didn't,
diff it and embed the difference in receiver.

---

## Phase 1+ — measurements

### M2 — cpp-sdk SHA in the mac app — `[INCONCLUSIVE — strings stripped of git rev]`
`strings` on `/Applications/LogosBasecamp.app/Contents/MacOS/{ui-host,logos_host}` (2026-06-12 build):
- Both binaries **contain** `[LogosObject] Remote EventHelper: dispatching event` and
  `RemoteLogosObject: connected EventHelper to QRemoteObjectReplica signals (IPC)`. → the dispatch-log
  capability is compiled in; its **absence in our relay logs = the inbound path genuinely isn't taken**,
  not a missing log statement. `[CONFIRMED]`
- No embedded cpp-sdk git SHA / version string found → can't confirm #79 from the binary. Resolving H3
  needs the **build manifest / nix store** of whoever built this app (owner `diana`), or a controlled
  rebuild (M3). `[? — unknown]`

### Mac toolchain / driving constraint — `[CODE]`
- `nix` present on mac (Determinate Nix 3.21.1) → can build darwin-arm64. ✓
- **No `logoscore`/`lm`/`pm` CLI** on this mac and no `~/logos-workshop` (those are Guru's). A headless
  `logoscore call voting …` send-trigger would require building the logoscore-cli too — **and** a CLI
  host likely runs a plain select() Qt dispatcher, **not** the GUI app's CoreFoundation/CFRunLoop
  dispatcher. So a CLI test would **not faithfully reproduce** the GUI-app CFRunLoop hypothesis (#4-H2).
- Core modules **load on-demand**: voting (core) won't `initLogos` until `voting_ui` is opened in the
  GUI sidebar. The §3.11 echo also needs a **send** (vote) to generate the echoed `messageReceived`.
  → The faithful decisive test = voting running **inside the GUI `LogosBasecamp.app`**, which needs
  (a) a GUI open of voting_ui, and (b) a vote trigger. Over pure ssh neither is automatable without a
  logged-in GUI session + automation (the prior relay run was GUI-driven by a human).

**→ Blocked on a user decision: how to drive the decisive GUI-app experiment on the mac.** Options in
the issue/checkpoint. Source survey (outcome #1) + the contradiction framing are complete regardless.
**Decision (Alisher): nail the build-version confound first, then proceed fully autonomous.**

### M3a — build-version confound: what cpp-sdk fixes exist, and did the failed test include them?

cpp-sdk fixes in scope (from logos-co/logos-cpp-sdk PRs, read from source):
- **#68** `d77c3dd6` merged **2026-05-25** — *"marshal provider events onto the source thread"*
  (`cpp/module_proxy.cpp`). Wraps `emit eventResponse(...)` in
  `QMetaObject::invokeMethod(this, …, Qt::AutoConnection)`. Its commit comment describes **our exact
  symptom**: "Emitting directly from a foreign thread runs QtRO's source serialization there, racing the
  source socket against a reply being sent from the source thread, **which can silently drop the
  reply**." → **#68 is the candidate fix for the 0-callback bug.** `[CODE — 90%]`
- **#79** `40e76314` merged **2026-06-08** — *"Marshal inter-module calls to the owner thread"*; adds
  `cpp/logos_thread_marshal.h` (`logos::runOnOwnerThread`, a detectable symbol). `[CODE]`

Provenance of the 2026-06-12 failed test, from nix locks + binaries:
- **Relay + delivery plugins** were built against cpp-sdk **`40e76314` (#79, incl #68)** — newest.
  (receiver `flake.lock` → `delivery_module/logos-module-builder/logos-cpp-sdk = 40e76314`.) `[CODE]`
- **App host** (`/Applications/LogosBasecamp.app` `logos_host`+`ui-host`, the binary that runs the QtRO
  source ModuleProxy + replica EventHelper for every core-module sidecar) is **almost certainly
  PRE-#68/#79**: `runOnOwnerThread` symbol **absent** from both host binaries; `logos-app`'s flake.lock
  pins cpp-sdk to `25c88f4` (**2026-05-03**, pre-#68); the app is **not** diana's local build (no
  logos-app source/result on the mac, `TeamIdentifier=not set`, ver "1.0") → an older prebuilt release.
  `[H — 80%]` (provenance inference; not yet measurement-confirmed — old host symbol-absence can be
  inlining, so confirm by retest on a ≥#79 host).

**Architecture note `[CODE — 70%]`:** each `type:core` module runs in its own `logos_host` sidecar; all
sidecars exec the **same app `logos_host` binary**. The ModuleProxy that forwards delivery's events
(source side, where #68 lives) is created by that host runtime. So whether #68 is *active* tracks the
**app host's** cpp-sdk, not the freshly-built delivery plugin's. (Plugin vs host static-link resolution
of cpp-sdk symbols on macOS two-level namespace is the one unverified link — the retest settles it.)

**Reframing of #4's root cause.** "Both ui-host AND logos_host fail" was read in #4 as evidence for a
deep platform bug (CFRunLoop, candidate #2). But CFRunLoop/QTBUG-39488 should affect **only** the GUI
ui-host — a headless `logos_host` (QCoreApplication, kqueue dispatcher) pumps sockets fine. The
**stale-host-cpp-sdk** explanation fits "both fail" *better*: a pre-#68 host drops provider events on
the source-thread race regardless of which sidecar consumes them. → **H3 (build version) promoted; H2
(CFRunLoop) demoted for the core-relay case.**

**Decisive next test (M3b):** put the already-built relay on a host with cpp-sdk **≥ #79** and retest.
- if `messageReceived` now fires → root cause was the **stale host cpp-sdk**, the bug is **fixed
  upstream**, receiver works on mac → revert the relay, ship the direct ui_qml consumer, close #4.
- if still 0 → genuine platform bug, now with the confound **controlled**.
Need a ≥#79 mac app/host: prefer a newer prebuilt release; else build logos-app at ≥#79 on the mac.

### M3b status — building (2026-06-12 ~10:21 CEST)
- **No prebuilt release qualifies:** every logos-app release pins cpp-sdk ≤ `ecd369d4` (2026-04-22).
  But **logos-app `master` bumped cpp-sdk to `f5a127d` (2026-06-11)** — includes #68, #79, #76/#81, #85.
- Launched on mac (bg PID 86742, log `~/logos-app-build-79.log`):
  `nix build github:logos-co/logos-app#bin-macos-app -o ~/result-app-79` (logos-app master `2e7c9a1`).
  → produces the full bundled `LogosBasecamp.app`, **identical shape to the failing app, only cpp-sdk
  ≥#79 differs** = clean single-variable control. Build in progress (reached Qt-bundle stage).

### Retest runbook (execute when build completes)
1. **Back up** `/Applications/LogosBasecamp.app` → `.app.pre79.bak`.
2. **Install new app:** `cp -R ~/result-app-79/LogosBasecamp.app /Applications/` (replace).
   Confirm `runOnOwnerThread` now PRESENT in its `logos_host`/`ui-host` (sanity: new cpp-sdk in).
3. **Modules:** keep installed `delivery_module` (#79-built) + `receiver_relay` (variant=darwin-arm64).
   **`receiver_ui` is NOT installed** (profile shows only delivery+relay) → install the ui_qml plugin
   (from diana's `~/receiver-basecamp/receiver_ui/result` or rebuild) into `PROF/plugins/receiver_ui`,
   `variant=darwin-arm64` — needed so opening it loads the relay.
4. **Launch** with captured logs (logos_host stderr IS captured; ui-host swallowed per basecamp#163):
   `open -W -n /Applications/LogosBasecamp.app --stdout /tmp/bc79.log --stderr /tmp/bc79.log &`.
   The relay also writes `/tmp/receiver-diag.log`.
5. **Load the relay:** open `receiver_ui` in the sidebar (only manual GUI step; test app auto-restore
   first — if it reopens last module, zero clicks). Relay auto-runs `startDiscovery` at +3s →
   `ensureDeliveryNode` + `subscribe(directoryTopic /radio-basecamp/1/directory/json)`.
6. **Generate a received event:**
   - (a) **ambient:** a live radio host on the directory topic (prior run saw 16–49 emits). Verify at
     test time by tailing `[delivery_module] emitEvent "messageReceived"`.
   - (b) **self-echo fallback** (no external host): the relay's `announceOnce` sends on `m_announceTopic`
     = `directoryTopic()` for public visibility, which it is subscribed to → logos.dev fleet echoes it
     back → self `messageReceived`. Trigger via the relay's host/announce path.
7. **Verdict greps** on `/tmp/bc79.log`:
   - `dispatching event "messageReceived" .* \(via IPC\)`  ← the line that NEVER appeared before
   - `EVENT messageReceived FIRED`  (relay diag, `receiver_relay_plugin.cpp:595`)
   - `ReceiverRelayPlugin: discovered station` / `ingestAnnounce`
   N≥3 loads. **Fires → H3 CONFIRMED (stale-host-cpp-sdk; fixed upstream).** **Still 0 → H2; platform
   bug with confound controlled.**

**Open risk:** the single GUI open (step 5) over ssh — test app auto-restore; else AppleScript/cliclick
(needs Accessibility; an earlier System-Events probe hung — may be a permission wall) or the
inspector-enabled app variant (`#integration-test-bundle`) which drives QML via logos-qt-mcp.

### M3b execution log
- **#79 app built** (`~/result-app-79`, store `pbdza15…`). Control validated by `nm | c++filt`:
  new `logos_host` HAS `logos::runOnOwnerThread` instantiated for `requestObject`/`invokeRemoteMethod`/
  **`onEvent`**; old `/Applications` `logos_host` has **none**. `[CONFIRMED]` So old host = pre-#68/#79,
  new host = #68/#79 present. (Earlier `strings`-based "absent" check was unreliable — template symbol,
  not a literal; `nm` is authoritative.)
- Can't replace `/Applications` (root-owned, no passwordless sudo) → ran new app from `~/result-app-79`
  via `open`. App launched; system sidecars (capability/package_manager/package_downloader) loaded, but
  **relay did NOT auto-load** (on-demand; needs `receiver_ui` view opened) and the app does **not**
  restore last module (no QSettings/state persistence found).
- **GUI-open is blocked over ssh:** `osascript` → `-1719 not allowed assistive access` (TCC). No URL
  scheme in Info.plist. CGEvent/cliclick would hit the same TCC wall.

### Pivot — headless via `logoscore` (clean control, no GUI/TCC)
`logos-logoscore-cli` is the **headless** CLI frontend of liblogos: a daemon that loads modules →
**spawns `logos_host` subprocesses (real cross-process QtRO IPC)** → stays alive for async events. Same
sidecar architecture as the GUI app, minus the Aqua/TCC dependency. logoscore-cli master pins cpp-sdk
**`f5a127d` (2026-06-11)** = #68/#79 in. So a logoscore daemon is a **#79 host** I can drive entirely
over ssh. This tests the exact contested path (core-sidecar delivery `onEvent` over IPC).

Headless retest (M3b'): `nix build …#cli-bundle-dir` on mac → verify spawned `logos_host` has
`runOnOwnerThread` → `logoscore -D -m <profile/modules>` → `load-module receiver_relay` (auto-loads
delivery dep; relay auto-`startDiscovery` → subscribes `/radio-basecamp/1/directory/json`) →
`call delivery_module send <topic> <payload>` (logos.dev fleet echoes → own `messageReceived`) → grep
sidecar stderr for `EVENT messageReceived FIRED` (relay diag, line 595) + `dispatching event … (via IPC)`.
Fires → **H3 CONFIRMED** (stale host was the cause; fixed upstream). 0 → **H2** (platform bug, controlled).
Bonus: needs neither `receiver_ui` nor a GUI.

### ✅ M3b' RESULT — `[CONFIRMED — 95%]` core delivery onEvent dispatches on mac with cpp-sdk #79
Host: `~/result-logoscore/bin/logos_host`, `nm | grep -c runOnOwnerThread = 4` (#79 in; old host = 0).
Daemon `-m <profile/modules>`; `load-module receiver_relay` (auto-loaded delivery dep, capability
token issued, node created, subscribed `/radio-basecamp/1/directory/json`). Raw (2026-06-12 10:37–10:39):
```
[delivery_module] emitEvent: "messageReceived"  →  ModuleProxy: forwarding event
[receiver_relay]  Remote EventHelper: dispatching event "messageReceived" to 1 callback(s) (via IPC)
[receiver_relay]  ReceiverRelayPlugin: EVENT messageReceived FIRED argc= 4
```
**Counts emitEvent : dispatching(via IPC) : EVENT FIRED = 7 : 7 : 7 (100%, zero drops)** over ambient
logos.dev directory traffic; +2 FIRED on a fresh module reload (reproducible). Same plugin binaries as
the failed #4 run — only the host cpp-sdk changed (pre-#68 → #79).

**Verdicts:**
- **H3 [H 35% → CONFIRMED 95%]** — stale host cpp-sdk was the cause.
- **H2 [H 60% → REJECTED]** for the core-sidecar path — core modules run in headless `logos_host`
  sidecars in both logoscore AND the GUI app, so this applies to the GUI app's core path too. CFRunLoop
  only governs the GUI **ui-host** (ui_qml C++ backend) path, which the relay architecture avoids.
- **H1 [H 65% → updated]** — the workshop's "voting receives on mac" claim is **vindicated in
  principle**: core-sidecar delivery events DO dispatch on mac on a ≥#68 host. The workshop omits the
  build-version caveat; no released basecamp ships #68 yet.

**Still open (not measured):** GUI **ui-host** (CFRunLoop) path for a `ui_qml` C++ backend on #79 —
logoscore is headless (no ui-host), and the GUI path is TCC-blocked over ssh. Provider-side #68 is
host-agnostic so it likely fixes that too, but unmeasured.

**Side-observation (minor, receiver decode):** under this host delivery delivers `data[2]` as
already-decoded payload bytes; the relay's `fromBase64` then garbles it → revisit the receiver decode
(single vs zero decode) — unrelated to dispatch.

### M1 — hackyguru `voting` on the #79 host — `[partial: blocked by delivery API drift, NOT dispatch]`
Built voting darwin-arm64 (cpp-sdk bumped to #68, then re-overridden to #79 to match delivery),
installed, loaded via logoscore on the #79 host. **All IPC plumbing worked**: capability token issued
(`requestModule voting→delivery_module` ok), `createNode` reached delivery (`DeliveryModuleImpl::createNode
called … callback called with ret: 0` = success). **But voting then `createNode returned false:
QVariant(LogosResult, )`** (empty result) and set `deliveryStatus=3`, never subscribing — so the receive
path was never reached. `[CONFIRMED cause]`
- **Root cause: delivery's `createNode` is now ASYNC** — it returns an empty `LogosResult` immediately
  and signals real success via callback later. The workshop's voting (≈2026-04) expects a **synchronous**
  success bool, so it bails before the node exists. Our **relay does NOT gate on the createNode result**
  (it proceeds to `subscribe`), which is exactly why the relay received and voting didn't. Same-class as
  the halt's "delivery pin discipline / API drift → Invalid response". Independent of cpp-sdk and of mac.
- **Implication:** the workshop voting module is **stale vs current `delivery_module`** (createNode
  contract) → file to hackyguru/logos-workshop. Its *architecture/claim* (core-sidecar onEvent on mac)
  is sound and proven by the relay; only its createNode result-handling is outdated.
- **Side-observation (payload encoding drift):** in the relay run, delivery delivered `data[2]` as the
  **raw** announce JSON bytes (not base64); the relay's single `fromBase64` garbled it. On this delivery
  version the payload is not base64 → the receiver decode needs adjusting (zero decode, not one). Another
  delivery API drift, orthogonal to dispatch.

### M4 — `logos-delivery-demo` (ui-host typed SDK) — `[NOT MEASURABLE headlessly]`
delivery-demo's receive runs in the GUI **ui-host** (CFRunLoop) via the typed `LogosModules.on()`.
logoscore is headless (no ui-host), and the GUI path is TCC-blocked over ssh. So the **ui-host CFRunLoop
path on #79 remains the one unmeasured cell.** Provider-side #68 is host-agnostic (it fixes the source
side regardless of consumer host), so it very likely fixes ui-host too — but that needs a GUI-app
measurement (someone at the mac, or an inspector-enabled build).

### Fail log (Sina honesty)
- **Fail:** I initially read "#79 symbol absent" from `strings` and treated it as near-evidence the old
  host lacked the fix. `strings` cannot see template instantiations; only `nm` could. **Fix:** symbol
  presence checks use `nm`/`nm|c++filt`, never `strings`, for C++ template/inline symbols.
- **Fail:** assumed the mac app was diana's local build; it was an older prebuilt. **Fix:** confirm app
  provenance (result symlink / nm of fix symbols) before reasoning about which cpp-sdk a binary has.
