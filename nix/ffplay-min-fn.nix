# #75 Minimal ffplay as a function of an `ffmpeg` (so both the standalone build and the flake-native
# helper bundle share one definition). See ffplay-min.nix for the standalone entry + the full rationale
# (nixos-24.05 for classic SDL2, --disable-everything, --disable-asm, doCheck=false).
{ ffmpeg }:
(ffmpeg.override { withSdl2 = true; }).overrideAttrs (o: {
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
