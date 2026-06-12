{
  description = "receiver_ui — listen-only QML view; drives receiver_relay via logos.callModule (mac workaround, feat/mac-core-relay).";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # receiver_relay is auto-resolved from metadata.json `dependencies`; attr name must match.
    receiver_relay.url = "path:../receiver_relay";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
