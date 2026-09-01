{
  description = "Logos Storage Module";

  # Pull pre-built artifacts from the self-hosted Logos Attic cache(Nix binary cache).
  nixConfig = {
    extra-substituters = [ "https://cache.nix.logos.co/public" ];
    extra-trusted-public-keys = [ "public:l4HrXgL4nw246+LBh2SOJyhz64BoGegOYLheT/iIAPU=" ];
  };

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # MERGE BLOCKER: a personal fork on a mutable branch (logos-storage-nim#1519).
    # Repoint at logos-storage/logos-storage-nim and a release tag before merge.
    logos-storage.url = "git+https://github.com/gmelodie/logos-storage-nim?submodules=1&ref=refs/heads/feat/use-nim-ffi";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    let
      nixpkgs = logos-module-builder.inputs.nixpkgs;
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];

      # The nim-ffi bindings encode their requests with TinyCBOR. The nixpkgs
      # archive is not position independent, so it cannot go into the plugin.
      # Shaped as a flake input so mkLogosModule resolves it per system.
      tinycbor = {
        packages = builtins.listToAttrs (map (system: {
          name = system;
          value.default =
            (import nixpkgs { inherit system; }).tinycbor.overrideAttrs (old: {
              cmakeFlags = (old.cmakeFlags or []) ++ [ "-DCMAKE_POSITION_INDEPENDENT_CODE=ON" ];
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
            for bin in ${unitTests}/bin/*; do
              name="$(basename "$bin")"
              if [ -n "$filter" ] && ! echo "$name" | grep -q "$filter"; then
                continue
              fi
              echo "=== $name ==="
              "$bin"
              ran=$((ran + 1))
            done
            if [ "$ran" -eq 0 ] && [ -n "$filter" ]; then
              echo "No test binary matched filter: $filter" >&2
              exit 1
            fi
          '';
        in {
          name = system;
          value = { tests = { type = "app"; program = toString runner; }; };
        }
      ) systems);

      existingApps = module.apps or {};
      mergedApps = builtins.listToAttrs (map (system: {
        name = system;
        value = (existingApps.${system} or {}) // (testsApps.${system} or {});
      }) systems);

      # Expose the test binaries as a buildable package so `nix build .#tests`
      # produces result/bin/{storage_module_tests,storage_module_integration_tests}.
      mergedPackages = builtins.listToAttrs (map (system: {
        name = system;
        value = (module.packages.${system} or {}) // {
          tests = testsPackage system;
        };
      }) systems);

    in module // { apps = mergedApps; packages = mergedPackages; };
}
