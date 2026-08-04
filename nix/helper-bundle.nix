# #75/#77/#78 The self-contained helper bundle shipped at <moduleDir>/bin/ inside the .lgx.
#
# Linux : ffplay + tor + privoxy + libpulse + libtorsocks + closures, rpath $ORIGIN, interp /lib64.
# Darwin: ffplay + tor + privoxy + closures, install-names rewritten to @loader_path, ad-hoc codesigned.
#         No libtorsocks (SIP blocks the LD_PRELOAD shim → mac onion uses privoxy), no libpulse
#         (SDL2 uses CoreAudio, a system framework). System dylibs (/usr/lib, /System) are left alone —
#         always present on macOS, the mac analogue of "system glibc is always there".
#
# ALL inputs come from ONE nixpkgs (nixos-24.05 — classic SDL2, and one glibc on Linux). The standalone
# Linux mirror is nix/build-bundle-bin.sh.
{ stdenvNoCC, lib, ffplayMin, tor, privoxy
, glibc ? null, patchelf ? null, libpulseaudio ? null, torsocks ? null   # linux
, cctools ? null, sigtool ? null                                          # darwin
}:
let isDarwin = stdenvNoCC.hostPlatform.isDarwin;
in stdenvNoCC.mkDerivation {
  pname = "receiver-helper-bundle";
  version = "1";
  dontUnpack = true;
  nativeBuildInputs = if isDarwin then [ cctools sigtool ] else [ patchelf glibc.bin ];
  buildCommand =
    if isDarwin then ''
      mkdir -p $out
      # copy a Mach-O file + recursively its /nix/store dylib deps (system /usr/lib + /System left alone)
      collect() {
        otool -L "$1" | tail -n +2 | awk '{print $1}' | while read -r dep; do
          case "$dep" in
            /nix/store/*) b=$(basename "$dep");
              if [ ! -e "$out/$b" ]; then install -m755 "$dep" "$out/$b"; collect "$out/$b"; fi ;;
          esac
        done
      }
      for src in ${ffplayMin}/bin/ffplay ${tor}/bin/tor ${privoxy}/bin/privoxy; do
        install -m755 "$src" "$out/$(basename "$src")"; collect "$out/$(basename "$src")"
      done
      # rewrite every install-name to @loader_path (so the bundle is relocatable), then ad-hoc re-sign
      # (install_name_tool invalidates the Mach-O signature; macOS refuses to run an unsigned-after-edit binary)
      for f in $out/*; do
        install_name_tool -id "@loader_path/$(basename "$f")" "$f" 2>/dev/null || true
        otool -L "$f" | tail -n +2 | awk '{print $1}' | while read -r dep; do
          case "$dep" in /nix/store/*) install_name_tool -change "$dep" "@loader_path/$(basename "$dep")" "$f" || true ;; esac
        done
        codesign -f -s - "$f" 2>/dev/null || true
      done
    '' else ''
      mkdir -p $out
      GLIBC_SKIP='libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|ld-linux|libresolv|libstdc\+\+|libgcc_s'
      add() {
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
      TSO=$(find ${torsocks}/lib -name 'libtorsocks.so.0.0.0' | head -1)
      add "$TSO"; install -m555 "$(readlink -f "$TSO")" "$out/libtorsocks.so"
      chmod -R u+w $out
      for f in $out/*; do patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true; done
      for exe in ffplay tor privoxy; do
        patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 "$out/$exe" 2>/dev/null || true
      done
    '';
}
