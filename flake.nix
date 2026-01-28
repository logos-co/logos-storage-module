{
  description = "Logos Storage Module";

  inputs = {
    # Follow the same nixpkgs as logos-liblogos to ensure compatibility
    nixpkgs.follows = "logos-liblogos/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-liblogos.url = "github:logos-co/logos-liblogos";
    logos-storage.url =  "git+https://github.com/logos-storage/logos-storage-nim?submodules=1";
  };

  outputs = { self, nixpkgs, logos-cpp-sdk, logos-liblogos, logos-storage }: 
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosLiblogos = logos-liblogos.packages.${system}.default;
        logosStorageNim = logos-storage.packages.${system}.libstorage;
      });
    in
    {
      packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosStorageNim }:
        let
          # Common configuration
          common = import ./nix/default.nix { inherit pkgs logosSdk logosLiblogos logosStorageNim; };
          src = ./.;
          
          # Library package (plugin + libcodex)
          lib = import ./nix/lib.nix { inherit pkgs common src logosStorageNim; };
          
          # Include package (generated headers from plugin)
          include = import ./nix/include.nix { inherit pkgs common src lib logosSdk logosStorageNim; };
          
          # Combined package
          combined = pkgs.symlinkJoin {
            name = "logos-storage-module";
            paths = [ lib include ];
          };
        in
        {
          # Individual outputs
          lib = lib;
          include = include;
          
          # Default package (combined)
          default = combined;
        }
      );

      devShells = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosStorageNim }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtremoteobjects
          ];
          
          shellHook = ''
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
            export LOGOS_LIBLOGOS_ROOT="${logosLiblogos}"
            export LOGOS_STORAGE_ROOT="${logosStorageNim}"
            echo "Logos Storage Module development environment"
            echo "LOGOS_CPP_SDK_ROOT: $LOGOS_CPP_SDK_ROOT"
            echo "LOGOS_LIBLOGOS_ROOT: $LOGOS_LIBLOGOS_ROOT"
            echo "LOGOS_STORAGE_ROOT: $LOGOS_STORAGE_ROOT"
          '';
        };
      });
    };
}
