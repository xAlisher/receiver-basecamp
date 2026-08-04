# #75/#77 The self-contained helper bundle that ships at <moduleDir>/bin/ inside the .lgx —
# ffplay (minimal) + tor + privoxy + libpulse + libtorsocks, each with its shared-lib closure, all
# rpath'd to $ORIGIN so they run under the clean env cleanSpawnEnv() hands spawned children.
#
# ALL inputs come from ONE nixpkgs (the caller passes nixos-24.05 packages): mixing revisions mixes
# glibc versions (a current libsystemd needs GLIBC_ABI_GNU2_TLS the 24.05 glibc lacks). ffplay needs
# 24.05 anyway (classic SDL2, not the sdl2-compat→SDL3 shim). Audio: we bundle libpulse (the backend
# forces SDL_AUDIODRIVER=pulse) — NOT libasound, whose nix build hardcodes its plugin dir to the store.
#
# The mirror of this logic for standalone/dev use is nix/build-bundle-bin.sh — keep them in sync.
{ stdenvNoCC, lib, glibc, patchelf, tor, privoxy, libpulseaudio, torsocks, ffplayMin }:
stdenvNoCC.mkDerivation {
  pname = "receiver-helper-bundle";
  version = "1";
  dontUnpack = true;
  nativeBuildInputs = [ patchelf glibc.bin ];
  buildCommand = ''
    mkdir -p $out
    GLIBC_SKIP='libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|ld-linux|libresolv|libstdc\+\+|libgcc_s'
    add() {   # copy a binary/lib + its (non-glibc) shared-lib closure into $out
      local src; src=$(readlink -f "$1")
      install -m555 "$src" "$out/$(basename "$1")"
      ldd "$src" 2>/dev/null | awk '/=>/{print $3}' | while read -r libp; do
        [ -z "$libp" ] && continue
        b=$(basename "$libp"); echo "$b" | grep -qE "$GLIBC_SKIP" && continue
        [ -e "$out/$b" ] || install -m555 "$(readlink -f "$libp")" "$out/$b"
      done
    }
    add ${ffplayMin}/bin/ffplay
    add ${tor}/bin/tor
    add ${privoxy}/bin/privoxy
    LP=$(find ${lib.getLib libpulseaudio}/lib -name 'libpulse.so.0.*' | head -1)
    add "$LP"; install -m555 "$(readlink -f "$LP")" "$out/libpulse.so.0"
    find ${lib.getLib libpulseaudio}/lib -name 'libpulsecommon-*.so' -exec install -m555 {} "$out/" \;
    # libtorsocks: LD_PRELOAD'd into ffplay for .onion (Linux); the wrapper hardcodes its store prefix.
    TSO=$(find ${torsocks}/lib -name 'libtorsocks.so.0.0.0' | head -1)
    add "$TSO"; install -m555 "$(readlink -f "$TSO")" "$out/libtorsocks.so"
    chmod -R u+w $out
    for f in $out/*; do patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true; done
    # The spawned binaries run as their own processes, so PT_INTERP must be an ABSOLUTE path that exists
    # on the TARGET (no nix store there). Use the x86-64 ABI-standard loader path — present on both merged
    # and split /usr systems (the portable bundler otherwise sets /lib/ld-linux-*, missing on split-/usr).
    for exe in ffplay tor privoxy; do
      patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 "$out/$exe" 2>/dev/null || true
    done
  '';
}
