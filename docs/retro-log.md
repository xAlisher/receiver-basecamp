# Retro Log

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
