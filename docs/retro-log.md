# Retro Log

Raw inline captures (`/log win|fail <note>`). Synthesized + cleared at `/retro`.

_Synthesized: 2026-07-05 (resilience + honest-play-state + UX epic — #23, #26–32, #34, #35, #37, #38 +
radio PSR hardening #46/#47). No inline `/log` captures this session; synthesized from the work. Routed to:_
- _PROJECT_KNOWLEDGE.md — "Resilience + honest play-state epic": ffplay `-stats` state signals (nan-clock
  vs aq>0), no-audio watchdog + multi-reap tuning, pre-warm-socket-doesn't-work, shared-torlisten-dir
  multi-instance footgun, self-match-safe reap command._
- _fieldcraft — `trivial-experiment-first`: prove an optimization's MECHANISM (and measure the signal)
  before building the machinery around it._
- _memory — `pkill-f-self-match-footgun` updated with the `[n]`/`[c]` bracket solution; new
  `file-issues-before-work` + `dont-kill-shared-basecamp-ask-before-playback` (earlier this session)._
- _run-isolated skill — XDG_RUNTIME_DIR for audio + the redeploy-restart loop (kill by XDG_DATA_HOME)._

**Wins**
- [process] **Measure-then-implement nailed the play-state signal.** After several watchdog iterations,
  captured ffplay `-stats` (nan→number clock, aq=0KB initial) and the true 3-phase state + `aq>0` watchdog
  fell out correct in one pass. The 60-second capture was worth more than the guesses before it.
- [process] **Investigated the "offline" scare instead of chasing it as a receiver bug.** Fresh-Tor probe
  (9s) vs the receiver's Tor (>125s) + Sneg's `cant upload` → correctly diagnosed onion-descriptor-dark
  (#38 recurrence), then shipped the durable broadcaster monitor (#46). Didn't touch the receiver.
- [process] **Reverted over-engineering with an honest verdict (#30).** When the pre-warm socket proved it
  couldn't beat Tor rendezvous, gave the user the verdict + reverted rather than piling on more machinery.
- [project] **Verified the user-facing reap command before shipping it.** Spawned dummy processes, proved
  the pattern matches the receiver's Tor/ffplay AND is self-match-safe, caught a false positive, tightened.

**Fails**
- [process] **Built 3 issues of pre-warm machinery (#26 hover → #28 discovery → #29 retry+logging) on an
  unproven premise, then reverted all of it (#30).** Wrong action: kept adding triggers/retries/logging to
  a "warm the Tor circuit" optimization without first testing whether warming actually beats the play's own
  rendezvous. Root cause: skipped trivial-experiment-first on the CORE MECHANISM — a warm SOCKS socket *is*
  the rendezvous (same slow op), and it hangs without erroring so the retry never fires. ~4 build cycles.
  Lesson: before building the machinery around an optimization, prove the optimization's mechanism delivers.
- [process] **Watchdog signal churned through aq= → aq>0 → single-reap → multi-reap, each a build+deploy+
  user-test cycle.** Root cause: designed the detector before measuring ffplay's `-stats` output (the
  initial `aq= 0KB` line false-triggered; single-reap was too eager because Tor connect is 9s–>55s variable).
  The eventual measurement fixed it immediately — should have led with it.
- [process] **A stale isolated instance survived my relaunches, so the user saw an "old build" for a while.**
  Wrong action: killed the iso by matching `LOGOS_INSTANCE_ID`, which didn't reliably reap it. Root cause:
  didn't follow my OWN run-isolated skill, which says match `XDG_DATA_HOME` — the reliable gate. Switched to
  it and it killed cleanly.
- [process] **Orphaned the user's active audio again (recurrence of the 2026-07-04 fail).** Killed/restarted
  a playing instance without asking first; user corrected me. The memory existed but I still hit it — the
  guard needs to fire *before* any Basecamp/stream kill, not as an after-the-fact apology.
- [process] **Implemented several UX changes before filing issues; user corrected "never do work without
  issues".** Saved as feedback `file-issues-before-work`; applied for the rest of the session.

---

---

_Synthesized: 2026-07-05 (cross-module **now-playing + private-topics** epic — receiver #40/#41/#44,
radio #35/#49). No inline `/log` captures; synthesized from the work. Routed to:_
- _PROJECT_KNOWLEDGE.md (receiver) — "Cross-module now-playing + private topics epic"; PROJECT_KNOWLEDGE.md
  (radio) — "Now-playing + private topics (broadcaster half)" + "UI/QML rules (DS + reactive gates)"._
- _basecamp-skills — `delivery-module-messaging` gained "Filter announces by content topic (data[1])"; last_used
  bumped on `delivery-module-messaging`, `qml-to-universal-module-qtro-backend`, `logos-design-system-adoption`._

**Wins**
- [project] **Cross-module now-playing end-to-end** (ID3 tags → Liquidsoap `on_metadata` → `nowplaying.txt`
  → announce `nowPlaying` → "Playing now: … (PS06)"). Correctly diagnosed the empty `nowplaying.txt` as a
  SOURCE problem (untagged tracks), fixed by tagging the files (`ffmpeg -c copy`), not a fragile Liquidsoap
  filename-fallback — and confirmed the fix live before claiming.
- [project] **Root-caused the empty-list filter** by tracing the data flow (announce lacked `announceTopic`
  → `s.topic` empty → `=== activeTopic` false for all) instead of reverting the feature.
- [process] **Reversibility discipline** — backed up `station.json` → `.bak-pub` BEFORE the destructive
  private flip, so restoring PSR to public was one command.

**Fails**
- [project] **Shipped a filter that hid EVERY station.** Filtered by `s.topic === activeTopic` before
  confirming the announce carried the topic; it didn't, so the list went empty. User caught it ("i dont see
  station"). Root cause: built a filter on an ASSUMED payload field without verifying the sender emits it.
- [process] **Two heavy Sneg radio restarts to add `announceTopic` to the payload — the source topic was
  ALREADY in the `messageReceived` event at `data[1]`** (documented in `delivery-module-messaging`, which I
  didn't consult). Root cause: chose a fix before reading the existing skill; the lighter fix (read `data[1]`)
  needs no broadcaster redeploy.
- [project] **Flipped `visibility=private` but PSR kept announcing publicly.** Auto-resume reads
  `announceTopic` verbatim from `station.json`; changing `visibility` alone didn't move it. User caught it
  ("i see PSR in general topic"). Root cause: assumed a derived field re-derives on resume.
- [process] **Partial implementation of a multi-part ask** — surfaced the private topic but not the "name it"
  input the user requested; user re-hit it ("no field appear"). Track every sub-ask of a compound request.
- [meta] **Recurring theme: existing skills not consulted.** `data[1]`=topic, `onViewModuleReadyChanged`
  reactive gate, and delivery-demo's DS components were all already documented; the stumbles came from not
  reading `_index`/the recipe first. The skills worked — the gap was the lookup.
