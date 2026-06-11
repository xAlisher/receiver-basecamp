# Retro Log

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
