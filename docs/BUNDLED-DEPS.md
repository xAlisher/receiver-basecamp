# Bundled dependencies — versions, security, and the bump process (#81)

Since **v0.3.0** (#75) the receiver ships its playback/privacy helpers **inside the signed `.lgx`** instead
of relying on the OS's `brew`/`apt` copies. That's the zero-install win — and the trade-off: **we own their
security updates.** System packages got CVE patches from the distro; bundled + pinned tools are frozen at our
build version until *we* re-bundle and re-release. This doc makes "track upstream manually" a real process.

## What's bundled (source of truth: [`nix/bundled-versions.txt`](../nix/bundled-versions.txt))

All from **nixpkgs `nixos-24.05`** (one glibc; classic SDL2). Current, as of `v0.3.0`:

| Tool | Version | Why it's here | Platforms |
|------|---------|---------------|-----------|
| ffmpeg (`ffplay`) | 6.1.2 | playback (HLS/AAC decode → SDL out) | linux + macOS |
| tor | 0.4.8.13 | `.onion` listener SOCKS | linux + macOS |
| privoxy | 3.0.34 | HTTP→SOCKS bridge for `.onion` (macOS; SIP blocks torsocks) | linux + macOS |
| torsocks (`libtorsocks`) | 2.4.0 | `.onion` LD_PRELOAD shim (Linux) | linux |
| libpulse | 17.0 | SDL pulse backend (Linux audio) | linux |

macOS uses CoreAudio (no `libpulse`) and privoxy (no `libtorsocks`).

## Security priority (what to watch, worst-first)

1. **tor** — privacy-critical. A stale tor can weaken onion/circuit security. Watch the
   [Tor Project security advisories](https://blog.torproject.org/category/security-advisories/) and
   `tor-announce`. Bump promptly on any advisory.
2. **ffmpeg** — large CVE surface, but we build `--disable-everything` + a *small* enabled set
   (demuxers `hls,mpegts,aac,mov,mp3`; decoders `aac,mp3`; protocols `file,pipe,tcp,http,hls,data`; TLS via
   gnutls). **Only CVEs in that enabled surface matter** — most ffmpeg CVEs (other codecs/formats) are out of
   scope. Check [FFmpeg security](https://ffmpeg.org/security.html).
3. **privoxy** — [privoxy announcements](https://www.privoxy.org/announce.txt).
4. **libtorsocks / libpulse** — lower risk (thin shims), bump with the rest.

## The bump process

1. Pick a nixpkgs rev. **The pin is 24.05 specifically for classic SDL2** — current nixpkgs' `sdl2-compat`
   → SDL3 shim breaks the bundled audio (#75). If you move to a newer stable, either (a) re-verify audio on
   BOTH platforms, or (b) keep SDL2 from a pinned input and bump the rest. Everything must stay
   **single-glibc** (mixing nixpkgs revisions broke the build in #75 — `GLIBC_ABI_GNU2_TLS`).
2. Update the input: `nix flake lock --update-input nixpkgs-2405` (or repoint `flake.nix`).
3. Rebuild the bundle: `nix/build-bundle-bin.sh` → confirm the smoke test passes; note the new versions
   (`nix/print-bundled-versions.sh <bundle-dir>`).
4. Update [`nix/bundled-versions.txt`](../nix/bundled-versions.txt) (versions + `last_reviewed` + `shipped_in`).
5. Release per [the `/release` flow](../README.md) — build linux (`--impure`) + darwin (on the M1), sign both,
   merge to a multi-variant, propagate to the catalog. **Record the bundled tool versions in the release notes.**

## Cadence

- **Quarterly** review (the [`bundled-deps-watch`](../.github/workflows/bundled-deps-watch.yml) workflow opens a
  reminder issue with the current versions + the advisory links above).
- **Out-of-band** for any high-severity CVE in an *enabled* component (esp. tor).

## Related
- #75 self-contained bundling · #77 flake-native packaging · #80 flake purity / catalog CI · #81 this process
