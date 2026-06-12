{
  description = "receiver_relay — delivery discovery + playback relay (core module, runs in logos_host).";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nixpkgs.follows = "logos-module-builder/nixpkgs";
    # delivery_module auto-resolved from metadata.json `dependencies`. Pin to MAIN (not v0.1.1): main's
    # zerokit/RLN nix build (#49) is the one that builds on aarch64-darwin (v0.1.1 fails on darwin), and
    # its version matches what the platform runs. (Same pin as ../flake.nix for receiver_ui.)
    delivery_module.url = "github:logos-co/logos-delivery-module";
    delivery_module.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
