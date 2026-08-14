# Retro Log

Raw inline captures (`/log win|fail <note>`). Synthesized + cleared at `/retro`.

_Cleared 2026-08-14 — #90/#94/#96 fleet-migration outage + v0.4.0→v0.4.2 releases. No inline `/log`
captures (see the standing gap below); synthesized directly from the session._

**Wins/fails → PROJECT_KNOWLEDGE** "#90/#94/#96 fleet-migration outage + the v0.4.0 regression".
**Skills → basecamp-skills:** `delivery-preset-name-not-portable` (new, critical),
`delivery-createnode-once-per-process` (new, high), `ui-qml-file-diag-per-instance` (new, high);
`lgx-merge-multivariant-cross-machine` `last_used` bumped (applied successfully for the darwin build).
**Memory:** `logos-dev-fleet-cluster-migration` extended, `ui-qml-log-not-captured` added.

### Fails (synthesized)

- [project] **Shipped v0.4.0 broken to every user.** Fixed the cluster migration by naming
  `{"preset":"logos.test"}`, verified it end-to-end, released, published to the catalog. It was dead on
  any stock install: the `delivery_module` the resolver serves (v1.1.0) has only `logos.dev` in
  `liblogosdelivery.so` → `createNode` rejected, no node, amber pill, nothing in any log. Root cause:
  validated against the `delivery_module` that happened to be on the dev box (v0.2.0, which has the
  preset) instead of what a user gets. Worse — the same lesson (*dial peers explicitly, don't trust a
  preset name*) had been learned hours earlier while fixing Sneg, and was not carried across to the
  receiver.
- [process] **Guessed for several rounds before enabling logging.** On Sneg, booth's announce heartbeat
  looked broken; theorised about timers, gates, and reentrancy across many tool calls. One
  `QT_LOGGING_RULES="*.debug=true"` in `run-app.sh` produced `announced seq 0/1/2` immediately and showed
  the sends were being dropped at the IPC layer, not the heartbeat. Should have reached for
  observability before hypotheses.
- [process] **Upgraded Sneg's `delivery_module` to get the preset, and broke its IPC.** The newer module
  (manifestVersion 0.3.0) on the pinned 268 AppImage connected fine, then silently stopped delivering
  every sync `invokeRemoteMethod` ~35 s in — booth logged announce success while nothing reached the
  wire. Root cause: treated "newer module" as safe on an old host. Reverting and dialling `logos.test`
  peers from the *original* module was the correct move and needed no upgrade at all.
- [process] **`pkill -f` / `pgrep -f` self-match, three times.** Each time the pattern string was in my
  own command line, so the shell matched itself and the inventory came back wrong (once reporting
  "4 processes of mine" that were all the grep). Known footgun, already in memory; still repeated. Use
  `ps -eo` + explicit exclusion.
- [project] **Reported a build artifact from the wrong out-link.** Checked `result-lgx-portable/` (a
  stale symlink from an earlier session) instead of `./result`, and briefly concluded the fix had not
  landed in the binary. `nix build` without `-o` writes `./result`.

### Wins (synthesized)

- [project] **Root-caused a platform-wide outage from logs alone, in one pass.** `different clusterId
  reported: 2 vs 3` + a per-day count across the log history pinned the migration window to
  2026-08-08 23:28 → 08-10 13:45 and proved it was upstream, not the receiver — before touching code.
- [process] **A/B'd the fix headlessly against the real module before building.** `logoscore` daemon +
  the installed `delivery_module`, control vs test, 5-minute soak: 6/6 peers held, 0 mismatches, 0
  disconnects. That harness is what later exposed the v0.4.0 regression too — reused for v0.4.1 and
  v0.4.2.
- [process] **Challenged own recommendation when asked "are logos.test nodes stable?"** Had been
  repeating the maintainer's claim; measuring it instead revealed `logos.test` is on **cluster 2** and
  ships its own bootstrap peers, which *reversed* the A-vs-B recommendation and made the fix a deletion
  rather than a new magic number.
- [project] **The instrumentation paid for itself immediately.** `ingest ok: "Parallel Society Radio"
  verified=yes topic=…` answered in one line what had cost hours of inference.
- [process] **Mac build turned out not to be wetware.** #93 was filed as needing a human; the M1 is
  SSH-reachable, so it was buildable directly — the wetware tag was a wrong assumption worth correcting
  fast.

### Standing gap (unchanged from prior retros)

Three retros in a row now with **zero inline `/log` captures** — everything is reconstructed at retro
time from the session, which `wins-and-fails.md` explicitly says is below the bar (a fail entry needs the
moment, the wrong action, and the root cause, captured while the reasoning is hot). The reconstruction
above is honest but was only possible because the session was still in context.
