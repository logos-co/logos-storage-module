{
  description = "Logos Storage Module";

  # Pull pre-built artifacts from the self-hosted Logos Attic cache(Nix binary cache).
  nixConfig = {
    extra-substituters = [ "https://cache.nix.logos.co/public" ];
    extra-trusted-public-keys = [ "public:l4HrXgL4nw246+LBh2SOJyhz64BoGegOYLheT/iIAPU=" ];
  };

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # MERGE BLOCKER: no release has the nim-ffi bindings yet; repoint at a release tag.
    logos-storage.url = "git+https://github.com/logos-storage/logos-storage-nim?rev=f9e5382beae9b21042fe904146e2afbfdac182d5&submodules=1";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    let
      nixpkgs = logos-module-builder.inputs.nixpkgs;
      lib = nixpkgs.lib;

      # Native systems plus the "x86_64-windows" cross target.
      systems = logos-module-builder.lib.common.systems;

      # No tests for x86_64-windows: its Windows binaries cannot run on the Linux builder.
      nativeSystems = builtins.filter (s: s != "x86_64-windows") systems;

      # The nixpkgs tinycbor archive is not position independent, so the plugin needs this rebuild.
      tinycbor = {
        packages = builtins.listToAttrs (map (system: {
          name = system;
          value.default =
            (logos-module-builder.lib.common.mkPkgs system).tinycbor.overrideAttrs (old: {
              cmakeFlags = (old.cmakeFlags or []) ++ [ "-DCMAKE_POSITION_INDEPENDENT_CODE=ON" ];
              # mingw links bin/cbordump.exe, but the Makefile installs bin/cbordump.
              postBuild = (old.postBuild or "")
                + lib.optionalString (system == "x86_64-windows") "mv bin/cbordump.exe bin/cbordump\n";
            });
        }) systems);
      };

      module = logos-module-builder.lib.mkLogosModule {
        src = ./.;
        configFile = ./metadata.json;
        flakeInputs = inputs;
        externalLibInputs = {
          libstorage = {
            input = inputs.logos-storage;
            packages.default = "libstorage";
          };
          tinycbor = {
            input = tinycbor;
            packages.default = "default";
          };
        };
        # The tests build stages headers flat into ./lib and drops these subdirectories; the plugin build stages them read-only from the store.
        preConfigure = { externalLibs }: ''
          mkdir -p lib/generated lib/tinycbor
          chmod -R u+w lib
          cp -f "${externalLibs.libstorage}"/include/generated/*.h lib/generated/
          cp -f "${externalLibs.tinycbor}"/include/tinycbor/*.h lib/tinycbor/
        '';
        tests = {
          dir = ./tests;
        };
      };

      # Provide a custom tests package to build tests without in-build execution.
      testsPackage = system:
        (module.packages.${system}.unit-tests).overrideAttrs (old: {
          buildPhase = builtins.replaceStrings [ ''"$bin"'' ] [ ":" ] old.buildPhase;
        });

      testsApps = builtins.listToAttrs (map (system:
        let
          pkgs = import nixpkgs { inherit system; };
          unitTests = testsPackage system;
          runner = pkgs.writeShellScript "run-tests" ''
            filter="''${1:-}"
            ran=0
            failed=""
            for bin in ${unitTests}/bin/*; do
              name="$(basename "$bin")"
              if [ -n "$filter" ] && ! echo "$name" | grep -q "$filter"; then
                continue
              fi
              echo "=== $name ==="
              # Run every binary, then fail below if any of them failed.
              "$bin" || failed="$failed $name(exit $?)"
              ran=$((ran + 1))
            done
            if [ "$ran" -eq 0 ] && [ -n "$filter" ]; then
              echo "No test binary matched filter: $filter" >&2
              exit 1
            fi
            if [ -n "$failed" ]; then
              echo "=== FAILED:$failed ===" >&2
              exit 1
            fi
          '';
        in {
          name = system;
          value = { tests = { type = "app"; program = toString runner; }; };
        }
      ) nativeSystems);

      existingApps = module.apps or {};
      mergedApps = builtins.listToAttrs (map (system: {
        name = system;
        value = (existingApps.${system} or {}) // (testsApps.${system} or {});
      }) systems);

      # Expose the test binaries as a buildable package so `nix build .#tests`
      # produces result/bin/{storage_module_tests,storage_module_integration_tests}.
      mergedPackages = builtins.listToAttrs (map (system: {
        name = system;
        value = (module.packages.${system} or {})
          // lib.optionalAttrs (builtins.elem system nativeSystems) {
               tests = testsPackage system;
             };
      }) systems);

    in module // { apps = mergedApps; packages = mergedPackages; };
}
