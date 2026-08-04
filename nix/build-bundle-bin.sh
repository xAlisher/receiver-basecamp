#!/usr/bin/env bash
# #75 Assemble the self-contained helper bundle that ships inside the receiver module at
# <moduleDir>/bin/ — ffplay (minimal) + tor + privoxy + libpulse, each with its shared-lib closure,
# all rpath'd to $ORIGIN so they run under the clean env cleanSpawnEnv() hands spawned children.
# The backend (receiver_ui_backend.cpp) resolves the binaries via bundledBin().
#
# SINGLE GLIBC — everything is built from nixos-24.05. Mixing nixpkgs revisions mixes glibc versions:
# a current-nixpkgs libsystemd (pulled by tor) needs GLIBC_ABI_GNU2_TLS the 24.05 glibc lacks → load
# failure. ffplay needs 24.05 anyway (classic SDL2, not the sdl2-compat→SDL3 shim), so pin ALL of it.
#
# AUDIO: we do NOT bundle libasound — nix's libasound hardcodes its plugin dir to the nix store and
# can't find the host's pipewire/pulse ALSA plugin. Instead we bundle libpulse (24.05) and the backend
# forces SDL_AUDIODRIVER=pulse, so ffplay reaches the host pulseaudio / pipewire-pulse over the socket
# (protocol-stable, glibc-safe). Covers every modern desktop; a pure-ALSA host would need a fallback.
#
# Usage:  nix/build-bundle-bin.sh [out-dir]        (default: ./bundle-bin)
# Result: <out>/{ffplay,tor,privoxy} + *.so*  — copy to variants/<platform>/bin/ in the .lgx.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-"$HERE/../bundle-bin"}
NIXPKGS='github:NixOS/nixpkgs/nixos-24.05'
GLIBC_SKIP='libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|ld-linux|libresolv|libstdc\+\+|libgcc_s'

echo ">> building minimal ffplay (24.05, classic SDL2)…"
FFPLAY=$(nix build -f "$HERE/ffplay-min.nix" --no-link --print-out-paths 2>/dev/null | head -1)/bin/ffplay
echo ">> building tor + privoxy (24.05)…"
NET=$(nix build --impure --no-link --print-out-paths --expr \
  "let p=(builtins.getFlake \"$NIXPKGS\").legacyPackages.x86_64-linux;
   in p.buildEnv{name=\"rcv-net\";paths=[p.tor p.privoxy];}" 2>/dev/null | head -1)
echo ">> fetching libpulse (24.05)…"
PA=$(nix build --no-link --print-out-paths "$NIXPKGS#libpulseaudio" 2>/dev/null | head -1)
echo ">> fetching torsocks (24.05)…"
TS=$(nix build --no-link --print-out-paths "$NIXPKGS#torsocks" 2>/dev/null | head -1)

rm -rf "$OUT"; mkdir -p "$OUT"
add() {   # copy a binary/lib + its (non-glibc) shared-lib closure into the bundle
  local src; src=$(readlink -f "$1")
  cp -Lf "$src" "$OUT/$(basename "$1")"
  ldd "$src" 2>/dev/null | awk '/=>/{print $3}' | while read -r lib; do
    [ -z "$lib" ] && continue
    b=$(basename "$lib"); echo "$b" | grep -qE "$GLIBC_SKIP" && continue
    cp -Lnf "$lib" "$OUT/$b" 2>/dev/null || true
  done
}
add "$FFPLAY"
add "$NET/bin/tor"
add "$NET/bin/privoxy"
# libpulse + the libpulsecommon it dlopens (SDL2's pulse backend needs both)
LP=$(find "$PA" -name 'libpulse.so.0.*' | head -1); add "$LP"; cp -Lf "$LP" "$OUT/libpulse.so.0"
find "$PA" -name 'libpulsecommon-*.so' -exec cp -Lf {} "$OUT/" \; 2>/dev/null || true
# libtorsocks: the backend LD_PRELOADs this into the bundled ffplay for .onion playback (Linux) instead
# of the `torsocks` wrapper (which hardcodes its nix-store prefix and can't be relocated).
TSO=$(find "$TS" -name 'libtorsocks.so.0.0.0' | head -1); [ -z "$TSO" ] && TSO=$(find "$TS" -name 'libtorsocks.so*' -not -type l | head -1)
add "$TSO"; cp -Lf "$TSO" "$OUT/libtorsocks.so"
chmod -R u+w "$OUT"
for f in "$OUT"/*; do patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true; done

echo ">> bundle assembled: $OUT ($(du -sh "$OUT" | cut -f1), $(ls "$OUT"/*.so* | wc -l) libs)"
echo ">> smoke test (clean env):"
for b in ffplay tor privoxy; do
  printf '   %-8s ' "$b"; env -i HOME=/tmp "$OUT/$b" $([ "$b" = ffplay ] && echo -version || echo --version) 2>&1 | head -1
done
