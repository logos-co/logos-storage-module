{
  description = "Logos Storage Module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-storage.url =  "git+https://github.com/logos-storage/logos-storage-nim?submodules=1";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      externalLibInputs = {
        libstorage = {
          input = inputs.logos-storage;
          packages.default = "libstorage";
        };
      };
      tests = {
        dir = ./tests;
      };
    };
}
