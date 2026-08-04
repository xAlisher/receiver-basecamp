#!/usr/bin/env bash
# #75 Assemble the self-contained helper bundle that ships inside the receiver module at
# <moduleDir>/bin/ — ffplay (minimal) + tor + privoxy, each with its shared-lib closure, all
# rpath'd to $ORIGIN so they run under the clean env cleanSpawnEnv() hands spawned children
# (no LD_LIBRARY_PATH). The backend (receiver_ui_backend.cpp) resolves these via bundledBin().
#
# Usage:  nix/build-bundle-bin.sh [out-dir]        (default: ./bundle-bin)
# Result: <out>/{ffplay,tor,privoxy} + *.so*  — copy to variants/<platform>/bin/ in the .lgx.
#
# glibc loader/core libs are deliberately NOT bundled (libc/ld-linux/libm/libdl/libpthread/librt/
# libresolv) — they must match the host loader and are universally present + ABI-stable.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-"$HERE/../bundle-bin"}
GLIBC_SKIP='libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|ld-linux|libresolv'

echo ">> building minimal ffplay (nixos-24.05, classic SDL2)…"
FFPLAY=$(nix build -f "$HERE/ffplay-min.nix" --no-link --print-out-paths 2>/dev/null | head -1)/bin/ffplay
echo ">> building tor + privoxy…"
NET=$(nix build --impure --no-link --print-out-paths --expr \
  'let p=(builtins.getFlake "nixpkgs").legacyPackages.x86_64-linux;
   in p.buildEnv{name="rcv-net";paths=[p.tor p.privoxy];}' 2>/dev/null | head -1)

rm -rf "$OUT"; mkdir -p "$OUT"
add_bin() {
  local src; src=$(readlink -f "$1")
  cp -Lf "$src" "$OUT/$(basename "$1")"
  ldd "$src" 2>/dev/null | awk '/=>/{print $3}' | while read -r lib; do
    [ -z "$lib" ] && continue
    b=$(basename "$lib"); echo "$b" | grep -qE "$GLIBC_SKIP" && continue
    cp -Lnf "$lib" "$OUT/$b" 2>/dev/null || true
  done
}
add_bin "$FFPLAY"
add_bin "$NET/bin/tor"
add_bin "$NET/bin/privoxy"
chmod -R u+w "$OUT"
for f in "$OUT"/*; do patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true; done

echo ">> bundle assembled: $OUT ($(du -sh "$OUT" | cut -f1), $(ls "$OUT"/*.so* | wc -l) libs)"
echo ">> smoke test (clean env):"
for b in ffplay tor privoxy; do
  printf '   %-8s ' "$b"; env -i HOME=/tmp "$OUT/$b" $([ "$b" = ffplay ] && echo -version || echo --version) 2>&1 | head -1
done
