# #75 Standalone minimal-ffplay build (for nix/build-bundle-bin.sh + manual builds).
# The flake-native path imports the shared function directly. See ffplay-min-fn.nix for the rationale.
#   nix build -f nix/ffplay-min.nix   →  result/bin/ffplay
let
  pkgs = (builtins.getFlake "github:NixOS/nixpkgs/nixos-24.05").legacyPackages.x86_64-linux;
in
import ./ffplay-min-fn.nix { inherit (pkgs) ffmpeg; }
