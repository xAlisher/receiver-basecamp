# Retro Log

## [fail] 2026-06-11
Spent 3 build/relaunch cycles "reducing" the station-row dot↔name gap by changing one QML layout
property at a time (spacing 16→10 → adding Layout.alignment → removing Layout.alignment to "restore"
fillWidth) and each time told the user it was fixed — the gap never moved. Wrong action: editing
layout code blind and *asserting* the fix without verifying the render (headless screenshots are
black on this Wayland box, so I had no test loop and leaned on guesses). Root cause: fighting
RowLayout's slack-distribution with property tweaks instead of switching to a deterministic
anchor-based row (dot anchored left, name anchored to dot.right + fixed margin), which removes the
ambiguity entirely. Rule: when a layout gap won't budge after one informed change, stop tweaking
properties and pin positions with anchors; and never report a visual fix as done without the user's
eyes or a real screenshot confirming it.

## Week of 2026-06-11 — receiver_ui build + the 295 getClient hang

### Wins
- [project] Receiver proven end-to-end on 268: discover (green) + onion playback (audible). The module
  is correct — every doubt about receiver's own code is retired.
- [project] Isolated the 295 hang by **controlled comparison**: held delivery.so (byte-identical),
  receiver, and profile constant; swapped only the AppImage → getClient returns 1ms/268, hangs/295.
  One-variable isolation turned 15 cycles of speculation into a one-line conclusion.
- [process] **File-based diag was the turning point.** ui-host stderr is swallowed (#163); writing a
  timestamped trail to /tmp showed "getClient(delivery_module)" with no return — ground truth that
  ended the guessing. → extracted into `delivery-getclient-hang-295`.
- [process] User redirects ("match stash↔storage / isolate the working workaround", "prove it on 268",
  "no guesses, data") repeatedly broke me out of trial-and-error into measurement. Worth soliciting.

### Fails
- [process] Spent ~15 rebuild/restart cycles permuting init timing (sync / deferred / two-stage /
  token-seed / version-match) BEFORE adding the file-diag that revealed getClient itself blocks.
  Root cause: reasoned from downstream symptoms (spinner vs stuck) instead of instrumenting the actual
  blocking call. Rule: when stderr is swallowed, file-diag the suspect call FIRST, before any rebuild.
- [project] A two-instance port-60000 conflict (two delivery nodes) confounded the "deferred → discovery
  stuck" reading mid-investigation → wasted a "two-instance was the cause" detour. Root cause: didn't
  verify single-instance before concluding. Rule: assert instance count before reading IPC behavior.
- [project] Chased a "delivery version mismatch" (receiver v0.1.1 vs platform 1.0.0) as the cause —
  but metadata version is 1.0.0 on every tag; the git-tag pin never changes it. Root cause: conflated
  git tag with metadata version without checking the running module's manifest.

## [win] 2026-06-11
receiver_ui (new listen-only module) now **loads on the latest Basecamp (295)** and discovers the live
Sneg "Logos manifesto" station over delivery_module.

Root cause of the persistent sidebar spinner / handshake-timeout: constructing the typed
`LogosModules(api)` in `initLogos` **blocks the ui-host event loop** on the delivery_module
capability-token handshake (the "Failed to register token … delivery_module" path), so the QRO
view↔backend handshake never completes in the platform's ~2s window → spinner times out to the logo,
re-click spins again. The ui-host process is **alive the whole time (NOT a crash)** — confirmed by
watching a stable PID. Fix: call `setBackend(this)` FIRST to register the view source, keep `initLogos`
non-blocking, and defer `new LogosModules` + `createNode`/`subscribe` to `QTimer::singleShot(2500)` off
the init critical path. Also removed `dladdr`/`moduleDir` (pulled `dladdr@GLIBC_2.34`, a portability
risk) since the option-1 `resolveBin` is env→PATH only.

Earlier wins, same effort:
- Discovery end-to-end on 295: needed explicit `entryNodes` (the deployed logos.dev preset shipped
  `bootstrapNodes=0` → no peers) + best-effort `startDiscovery` (cross-version `LogosResult.success`
  reads false even when the call succeeded server-side).
- The `ui_qml`+C++ backend shape (delivery-demo) consumes delivery on the latest platform with no #31
  crash — the crash is process identity (core sidecar lacks the token), not the module's `type:` label.

Module: `~/basecamp/modules/receiver-basecamp`, branch `feat/milestone-2-delivery-init`.

## Week of 2026-06-12 — mac demo attempt: the core-relay workaround, disproven

### Wins
- [project] **Proved the mac event-dispatch bug is host-agnostic** with one instrumented test, not
  speculation. Hypothesis (research #4): ui-host can't receive delivery onEvent, but logos_host can →
  put the consumer in a `type:core` relay. Built relay + pure-QML ui, ran it, and a qDebug at the relay's
  event-lambda entry showed **49 delivery emits → 0 callbacks**. The QRO-replica IPC boundary, not the
  host, is where the event dies. Closed the hypothesis with data and pivoted the demo to Linux.
- [process] **One targeted probe beat more guessing.** "0 stations" was ambiguous (onEvent never fired
  vs ingest rejected the payload). Added a single `qDebug` at the lambda's first line → settled it
  instantly. trivial-experiment-first applied at the *diagnosis* step.
- [project] Built `receiver_relay` (type:core) + a pure-QML `receiver_ui` (QtObject shim polling the
  relay via `logos.callModule`) on **darwin-arm64** — the request/reply half of the pattern works on mac
  (getClient, invokeRemoteMethod, createNode/start/subscribe all return).

### Fails
- [project] **Built the entire relay+ui workaround on an unproven load-bearing assumption.** Took
  research #4's "ui-host-specific" framing as fact and never ran the cheap proof first — a ~10-line core
  module that just logs whether it receives one delivery event on mac. That probe would have disproven
  the premise *before* the relay scaffold, the pure-QML rewrite, two darwin builds, and the install
  cycles. Root cause: trusted a hypothesis labeled "research" as settled; trivial-experiment-first says
  probe the assumption the whole plan rests on, first.
- [project] **Burned a build/install/relaunch cycle on the dev `.#lgx` variant.** Rebuilt the relay with
  `nix build .#lgx` (→ `darwin-arm64-dev`, plugin dylib only, `/nix/store` linkage); it silently never
  loaded ("clicking the icon doesn't open the module"). Root cause: didn't compare the install layout to
  the *working* first install — the original used `.#lgx-portable` (bundled libboost/libssl/libcrypto).
  Assumed `.#lgx` == "the build that worked." → extracted `darwin-lgx-portable-required`.
- [project] Tried to validate the relay **in isolation** (relay installed, ui disabled) and saw nothing —
  no construct, no log. Root cause: profile core modules load **on demand**; with no consumer asking for
  it, the relay never loads. → extracted `darwin-core-module-on-demand-load`.

## Week of 2026-06-12 — delivery-on-mac: the confound, mac unblocked, onion playback fixed

### Wins
- [project] **Reversed the "mac is blocked" verdict with a controlled, single-variable test.** Prior
  conclusion (research #4 + skill): cross-module delivery events never dispatch on mac (platform/CFRunLoop
  bug, no workaround). Found the real cause: the failing host's cpp-sdk **predated #68** (the provider
  event-marshal fix). Rebuilt the host from logos-app master (cpp-sdk ≥#68), re-ran the *same* relay →
  `messageReceived` 7/7. The earlier "both hosts fail" was a stale-host artifact, not the platform.
- [process] **logoscore as a headless cross-process test host bypassed the GUI TCC wall.** AppleScript
  GUI automation is blocked over ssh (`-1719 not allowed assistive access`), so I couldn't drive the GUI
  app. logoscore spawns the *same* logos_host sidecars (real cross-process QtRO IPC) headlessly →
  faithful test of the core event path + privoxy playback, entirely over ssh.
- [process] **trivial-experiment-first, twice, before building.** (a) Detected the fix in a binary with
  `nm|c++filt`, not a rebuild. (b) Proved the privoxy→tor→onion playback path with a standalone
  privoxy+ffprobe PoC *before* writing a line of plugin code — de-risked the whole #7 fix.
- [project] **Mac now works end-to-end:** discovery + .onion Tor playback (privoxy bridge), verified in
  the GUI (audible). Shipped darwin-arm64 lgxs + docs.

### Fails
- [process] **Read "#79 symbol absent" from `strings` and nearly treated it as evidence the old host
  lacked the fix.** `strings` can't see C++ template instantiations; `runOnOwnerThread` only shows under
  `nm | c++filt`. Root cause: used a text-scan tool for a symbol-table question. Rule: symbol-presence in
  a binary → `nm`/`nm|c++filt`, never `strings`.
- [process] **Assumed the mac `/Applications` app was diana's local build.** It was an older prebuilt
  (TeamIdentifier unset, no source/result). Spent effort reasoning about its cpp-sdk before confirming
  provenance. Rule: confirm a binary's provenance (result symlink / fix-symbol via nm) before reasoning
  about which dependency version it carries.
- [project] **A prior skill (`darwin-cross-module-event-ipc-broken`, critical) shipped a wrong root
  cause** and would have steered future mac work to "give up / wait for a platform fix." Root cause: a
  confounded measurement promoted to a CONFIRMED platform verdict without controlling the host build
  version. Now deprecated + replaced (`darwin-delivery-events-need-cpp-sdk-68`). Rule: a cross-process
  "it's the platform" verdict must control the dependency (cpp-sdk) version before it's CONFIRMED.
