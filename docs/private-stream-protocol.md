# Private-Stream Protocol — Receiver side (receiver#69)

> **Status:** Phase 1 — **specification only, no crypto implemented.** Blocked on a human sign-off of
> the crypto primitive before any code (receiver#69 / booth#66).

The **canonical spec** — topic derivation, what the payload is, encryption, the primitive decision,
versioning, and the mandatory headless round-trip test — lives in the **Booth** repo (the protocol
origin, booth#66):

**→ [`booth-basecamp/docs/private-stream-protocol.md`](https://github.com/xAlisher/booth-basecamp/blob/main/docs/private-stream-protocol.md)**

This is a pointer, not a mirror, on purpose: the two ends must produce **byte-identical** topics and
keys, so there is exactly **one** normative copy of the byte-level rules. Do not fork the crypto
details into this file.

## Receiver's half of the contract (Phase 3, receiver#69)

Per Receiver **ADR-10**, once the primitive is signed off Receiver must, from the user-entered
**`Title` + `Pass`**:

1. **Derive the same topic** — `topic = /radio-basecamp/1/<base32(SHA-256(canonical-JSON(Title+Pass))[:16])>/json`
   (canonical bytes exactly as the spec §3 defines) and subscribe to it.
2. **Detect + decrypt** the announce — if the delivery payload is the encrypted envelope
   (`{pv,enc,n,ct}`, spec §4), derive the key from `Pass`, decrypt, **then** verify the inner
   signature (ADR-5). Plaintext announces (public / legacy) parse as today.
3. **Encrypt the ANNOUNCE, not the audio** — audio stays HLS-over-Tor from the origin (Receiver
   ADR-4); it is not touched.
4. **Ship the headless round-trip test** (spec §7): derive+decrypt must round-trip Booth's
   derive+encrypt for the same `(Title, Pass)`, with wrong-pass/tamper/replay failing closed, and the
   C++ **and** Android/JS ports asserting the golden cross-impl vectors.

**No revocation** (ADR-10): anyone with `Title+Pass` can listen indefinitely; rotating access means a
new `Title`/`Pass` re-shared out-of-band.
