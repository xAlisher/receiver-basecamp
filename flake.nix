{
  description = "Receiver — discover & listen to decentralized Logos radio broadcasts (listen-only, cross-platform)";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";  # #20: newest master (rev pinned in flake.lock). The universal sync-createNode deadlock is solved in-module via fire-and-forget async (see receiver_ui_backend.cpp + docs/universal-migration-fork-tree.md) — NOT a builder fix.
    # delivery_module is auto-resolved from metadata.json `dependencies` via this input.
    # Pin to MAIN (not a tag): main carries the zerokit/RLN nix build fix (#49, 2026-06-09) that fixes
    # the crates.io-403 which had forced us to v0.1.1 — AND main's version (1.0.0) MATCHES the delivery
    # the 295 platform actually runs. Building against v0.1.1 while the platform runs 1.0.0 left
    # getClient("delivery_module") hanging on a QRO/version mismatch. Matching the version is the fix.
    delivery_module.url = "github:logos-co/logos-delivery-module";
    delivery_module.inputs.logos-module-builder.follows = "logos-module-builder";
    # #75/#77 Single-glibc source for the self-contained helper bundle (ffplay+tor+privoxy+libpulse+
    # libtorsocks). Pinned to 24.05 for classic SDL2; mixing nixpkgs revisions mixes glibc → load fails.
    nixpkgs-2405.url = "github:NixOS/nixpkgs/nixos-24.05";
  };

  outputs = inputs@{ logos-module-builder, nixpkgs-2405, ... }:
    let
      # Build the helper bundle for the HOST system (each dev/release build is single-system, so
      # `builtins.currentSystem` is right — build with `--impure`). Linux → ffplay+tor+privoxy+libpulse+
      # libtorsocks ($ORIGIN); darwin → ffplay+tor+privoxy (@loader_path, CoreAudio) — see helper-bundle.nix.
      system = builtins.currentSystem;
      pkgs2405 = nixpkgs-2405.legacyPackages.${system};
      ffplayMin = import ./nix/ffplay-min-fn.nix { inherit (pkgs2405) ffmpeg; };
      helperBundle = pkgs2405.callPackage ./nix/helper-bundle.nix { inherit ffplayMin; };
    in
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      # #77 ship the self-contained helpers at <moduleDir>/bin/ so the built+signed .lgx is zero-install.
      postInstall = ''
        mkdir -p $out/lib/bin
        cp -a ${helperBundle}/. $out/lib/bin/
        chmod -R u+w $out/lib/bin
      '';
    };
}
