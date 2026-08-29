{
  description = "Logos Storage Module";

  # Pull pre-built artifacts from the self-hosted Logos Attic cache(Nix binary cache).
  nixConfig = {
    extra-substituters = [ "https://cache.nix.logos.co/public" ];
    extra-trusted-public-keys = [ "public:l4HrXgL4nw246+LBh2SOJyhz64BoGegOYLheT/iIAPU=" ];
  };

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-storage.url = "git+https://github.com/logos-storage/logos-storage-nim?submodules=1&ref=refs/tags/v0.4.4";
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
      lib = nixpkgs.lib;

      # The builder's own list, so this flake gains a target the moment the
      # builder does. It is currently the four native systems plus the
      # "x86_64-windows" pseudo-system (a mingw cross build, whose derivations
      # carry system = x86_64-linux and so realise on an ordinary Linux
      # builder). Hard-coding the list here is what kept this module off
      # Windows while every module that returns mkLogosModule directly gained
      # it for free.
      systems = logos-module-builder.lib.common.systems;

      # The test plumbing below is native-only, for two independent reasons:
      # `import nixpkgs { system = "x86_64-windows"; }` yields a NATIVE Windows
      # package set (it does not throw -- it produces something unusable, and
      # the first symptom is a misleading failure inside writeShellScript), and
      # a runner that executes ${unitTests}/bin/* would be running PEs on the
      # Linux builder regardless.
      nativeSystems = builtins.filter (s: s != "x86_64-windows") systems;

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
