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
      tests = {
        dir = ./tests;
        preConfigure = { externalLibs, ... }: ''
          mkdir -p lib
          if [ -d "${externalLibs.libstorage}/lib" ]; then
            cp ${externalLibs.libstorage}/lib/libstorage.* lib/ 2>/dev/null || true
            # Fix install_name so dyld can find the lib at runtime via RPATH
            if [ -f "lib/libstorage.dylib" ]; then
              install_name_tool -id @rpath/libstorage.dylib lib/libstorage.dylib 2>/dev/null || true
            fi
          fi
          if [ -d "${externalLibs.libstorage}/include" ]; then
            cp ${externalLibs.libstorage}/include/*.h lib/ 2>/dev/null || true
          fi
        '';
      };
    };
}
