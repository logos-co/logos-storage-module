{
  description = "Logos Storage Module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-storage.url = "git+https://github.com/logos-storage/logos-storage-nim?submodules=1&ref=fix/delete-dataset-crash";
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
      preConfigure = { externalLibs }: ''
        mkdir -p lib
        if [ -d "${externalLibs.libstorage}/lib" ]; then
          cp ${externalLibs.libstorage}/lib/libstorage.* lib/ 2>/dev/null || true
        fi
        if [ -d "${externalLibs.libstorage}/include" ]; then
          cp ${externalLibs.libstorage}/include/*.h lib/ 2>/dev/null || true
        fi
      '';
    };
}
