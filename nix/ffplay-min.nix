# #75 Minimal ffplay for the self-contained receiver bundle (zero external deps).
#
# Built from nixos-24.05 ON PURPOSE: current nixpkgs ships `SDL2` as the sdl2-compat shim that
# dlopens SDL3 at runtime — it fails to init audio even when SDL3 is bundled alongside. 24.05 still
# has CLASSIC SDL2 (2.30.x), which links ALSA directly and works from an $ORIGIN bundle with a clean
# env. `doCheck=false` because the 24.05 fate test suite doesn't build under `--disable-everything`.
#
# Strip to just the receiver's playback path: HLS/mpegts/aac/mov/mp3 demux, aac/mp3 decode, sdl2 out.
# `--disable-asm` drops the hand-written x86 asm (whose _x86 symbols dangle under --disable-everything).
#
# Build:  nix build -f nix/ffplay-min.nix   →  result/bin/ffplay  (+ result-lib etc.)
let
  pkgs = (builtins.getFlake "github:NixOS/nixpkgs/nixos-24.05").legacyPackages.x86_64-linux;
in
(pkgs.ffmpeg.override { withSdl2 = true; }).overrideAttrs (o: {
  pname = "ffmpeg-receiver-min";
  doCheck = false;
  configureFlags = o.configureFlags ++ [
    "--disable-ffprobe" "--disable-ffmpeg" "--disable-vaapi" "--disable-vdpau" "--disable-asm"
    "--disable-everything"
    "--enable-ffplay" "--enable-sdl2" "--enable-swresample" "--enable-swscale" "--enable-avfilter"
    "--enable-network" "--enable-protocol=file,pipe,tcp,http,hls,data"
    "--enable-demuxer=hls,mpegts,aac,mov,mp3" "--enable-decoder=aac,aac_latm,mp3"
    "--enable-parser=aac,aac_latm,mpegaudio" "--enable-filter=aresample,anull,aformat"
    "--enable-bsf=aac_adtstoasc"
  ];
})
