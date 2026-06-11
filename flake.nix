{
  description = "Receiver — discover & listen to decentralized Logos radio broadcasts (listen-only, cross-platform)";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # delivery_module is auto-resolved from metadata.json `dependencies` via this input.
    # Pinned to v0.1.1 (not the demo's v0.1.2): v0.1.1's zerokit builds locally, whereas v0.1.2's
    # zerokit-2.0.2 vendor step 403s on crates.io (missing UA). The createNode/start/subscribe/
    # messageReceived API we use is stable across both. `follows` aligns the Rust/zerokit toolchain.
    delivery_module.url = "github:logos-co/logos-delivery-module/v0.1.1";
    delivery_module.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
