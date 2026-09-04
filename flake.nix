{
  description = "Logos Storage Module";

  # Pull pre-built artifacts from the self-hosted Logos Attic cache(Nix binary cache).
  nixConfig = {
    extra-substituters = [ "https://cache.nix.logos.co/public" ];
    extra-trusted-public-keys = [ "public:l4HrXgL4nw246+LBh2SOJyhz64BoGegOYLheT/iIAPU=" ];
  };

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-storage.url = "git+https://github.com/logos-storage/logos-storage-nim?submodules=1&rev=efa14695a43aea235df3d71f31f4aa6f1f11f92c";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    let
      module = logos-module-builder.lib.mkLogosModule {
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

      nixpkgs = logos-module-builder.inputs.nixpkgs;
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];

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
