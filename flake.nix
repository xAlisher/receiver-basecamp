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
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
