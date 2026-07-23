# Receiver — Functional Scope (FURPS+)

> **Status:** Basecamp module v0.2.0.5 shipped (Linux + macOS/arm64) · Android v1.0.0 shipped
> **Scope:** Listening only — discover, verify, and play decentralized radio. Hosting is the [Booth](https://github.com/xAlisher/booth-basecamp)'s job.
> **Target:** Logos Basecamp (Linux x86_64, macOS arm64); Android 13+ (arm64, sideload). Depends on `delivery_module`.
> **Non-goals (this version):** hosting/broadcasting · TOFU key-rotation warnings · bundled runtime binaries

---

## Functionality (F)

### F1. Discover stations over Logos Messaging
- **F1.1**: Subscribe to the public directory topic `/radio-basecamp/1/directory/json` and user-supplied private topics.
- **F1.2**: Ingest JSON station announces broadcast by Booth/radio hosts.
- **F1.3**: Prune dead stations by heartbeat TTL (~45 s).

### F2. Verify station identity (secp256k1)
- **F2.1**: For `v:2` announces, reconstruct the canonical sig-less bytes and verify the ECDSA signature against the embedded pubkey.
- **F2.2**: **Drop** announces that fail verification (forgery/tamper).
- **F2.3**: Keep `v:1` (unsigned) as anonymous/unverified.
- **F2.4**: Render verified stations as `IP hidden by Tor · <3-word PGP fingerprint>`.
- **F2.5**: **Pin** a verified station by its public key (persists; follows the broadcaster across renames).

### F3. Play a station
- **F3.1**: Stream `.onion` HLS through a module-owned listener Tor into an `ffplay` subprocess.
- **F3.2**: Play direct (LAN) HLS URLs without Tor.
- **F3.3**: Honest playback state: connecting → caching → playing, with a live connection pill and jitter buffer.
- **F3.4**: No-audio watchdog: reap the listener Tor and retry (up to 3×) when audio never arrives.

### F4. Android (standalone app)
- **F4.1**: Run an on-device embedded Logos Messaging node (`liblogosdelivery` via JNI) — discover on the same topics/schema as desktop.
- **F4.2**: Verify signatures on-device (JS secp256k1) with the same canonical + fingerprint scheme.
- **F4.3**: Play `.onion` HLS over embedded Tor (kmp-tor); audio only.

---

## Usability (U)
- **U1**: Single design-system panel: live station list, verify/pin state, per-station connection status.
- **U2**: First-launch **dependency-preflight card** (tor/ffmpeg/torsocks|privoxy) with copy-able install commands + Re-check.
- **U3**: Fingerprint + "IP hidden by Tor" shown per station for honest trust signalling.

## Reliability (R)
- **R1**: Forged/tampered announces are dropped, never rendered.
- **R2**: Playback recovers from a dead Tor rendezvous via the reap-and-retry watchdog (F3.4).
- **R3**: `.onion` playback proven end-to-end on Linux (v0.2.0) and macOS/arm64 (v0.2.1); Android on-device discover→verify→play proven on device.

## Performance (P)
- **P1**: Listener jitter buffer smooths HLS/Tor variability.
- **P2**: Tor rendezvous first-connect is variable (~9–55 s); the UI reflects it honestly rather than faking "playing".

## Supportability (S)
- **S1**: Single `ui_qml` module with a QtRO C++ backend — ships on the latest platform and on macOS/arm where a core-sidecar consumer cannot.
- **S2**: Basecamp module dual-licensed MIT OR Apache-2.0; Android app MIT. Releases signed by xAlisher.
- **S3**: Shares a byte-identical identity contract with Booth (canonical JSON + SHA-256 + secp256k1); two verify implementations (C++, JS) kept in lockstep.

## + (design constraints)
- **+1**: Verify-only on the identity contract — no signing, no keycard dependency.
- **+2**: **Not TOFU** — signature verification + pin-by-pubkey, but no key-rotation / name-reuse warning yet.
- **+3**: `delivery_module` is a hard dependency (github `main` pin on Linux; installed via Package Manager as 0.1.3 on macOS); not bundled.
- **+4**: Runtime helpers (tor/ffmpeg/torsocks|privoxy) not bundled — blocked on `logos-module-builder#114`.
- **+5**: No blockchain; discovery + identity ride Logos Messaging only.
