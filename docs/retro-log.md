# Retro Log

Raw inline captures (`/log win|fail <note>`). Synthesized + cleared at `/retro`.

_Last synthesized: 2026-07-04 (design-system-ui epic). Outputs routed to:_
- _PROJECT_KNOWLEDGE.md — v0.2.0 unblocks getClient; two-Main.qml (DIRECT vs relay); design-system + live pill._
- _basecamp-skills — new: `verify-which-main-qml-the-build-bundles`, `delivery-connection-state-pill`; updated: `delivery-getclient-hang-295` (resolved on v0.2), `kill-logos-processes` (pkill -f self-match guard)._

_Synthesized: 2026-07-04 (universal-api-migration #20 + no-sound #12). Outputs routed to:_
- _PROJECT_KNOWLEDGE.md — universal migration works via fire-and-forget async; no-sound = .onion transport (descriptor/HLS/tor)._
- _basecamp-skills — new: `universal-modules-sync-call-deadlocks-ui-host` (critical); updated `qml-to-universal-module-qtro-backend` (last_used + cross-link)._
- _memory — `onion-playback-no-sound-playbook`. issues — #20 (validated), #12 (3 playback failure modes)._

**Wins**
- [project] Universal migration WORKS with zero upstream fixes — sync `modules().delivery_module.createNode()` deadlocks the ui-host; fire-and-forget async + event-push unblocks it. Validated headless AND in real Basecamp (discovery + pill + audio).
- [process] Red-team fork-tree held: forked logos-cpp-sdk, patched the generator (QEventLoop), PROVED it insufficient (`/proc/wchan=do_wait` → block is in the IPC consumer, not codegen), logged Node 7, did NOT upstream a non-working fix. The methodology's "keep home until proven" caught a wrong fix before it shipped.
- [ops] `TMPDIR=/extra/tmp` + two `nix-collect-garbage` passes kept root alive through ~10 heavy delivery rebuilds (root held 14–27G; GC freed 9.7G each).

**Fails**
- [process] Patched the cpp-generator (2× 15-min build cycles) BEFORE checking where the sync call actually blocks. Wrong action: assumed the generated wrapper's blocking `invokeRemoteMethod` was the deadlock site and forked the SDK to wrap it in a QEventLoop. Root cause: skipped the cheap locate-the-block step (`cat /proc/<pid>/wchan` = `do_wait`) that would have shown the block is in the IPC consumer's subprocess wait, deeper than codegen — the patch could never work. Lesson (now in the skill's Diagnose section): on a sync-IPC hang, read `/proc/wchan` FIRST, then choose the layer to fix.
- [project] Chased "no sound" as a receiver bug (ffplay flags, audio sinks) before the broadcaster onion descriptor — which was the actual root (`can't upload descriptor` ×599 → onion dark). Root cause: no top-down transport playbook existed; built one (`onion-playback-no-sound-playbook`, descriptor→HLS→tor).
- [process] On "kill leftovers" reaped ALL `torlisten` incl. the user's ACTIVE playback session. Root cause: matched the process pattern broadly without excluding the current session; should scope reaps to stale/orphaned only.

---
