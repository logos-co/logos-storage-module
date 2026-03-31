# Builds and runs the Qt Test suite for logos-storage-module.
#
# The tests/ directory is a standalone CMake project
{ pkgs, common, src, logosStorageNim }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-tests";
  version = common.version;

  inherit src;
  inherit (common) nativeBuildInputs buildInputs meta env;

  configurePhase = ''
    runHook preConfigure
    cmake -GNinja -S./tests -B./build ${pkgs.lib.concatStringsSep " " common.cmakeFlags}
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    ninja -C ./build
    runHook postBuild
  '';

  # On macOS, libstorage.dylib's LC_ID_DYLIB install name is set to its Nix
  # build-sandbox path (e.g. /nix/var/nix/builds/nix-XXXXX/source/build/libstorage.dylib),
  # which no longer exists after that derivation's build completes.  Fix the
  # reference recorded inside the test binary so dyld can find it at runtime.
  postBuild = pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
    bad=$(${pkgs.darwin.cctools}/bin/otool -L ./build/storage_module_tests \
          | ${pkgs.gawk}/bin/awk '/libstorage/{print $1}')
    if [ -n "$bad" ]; then
      ${pkgs.darwin.cctools}/bin/install_name_tool \
        -change "$bad" "${logosStorageNim}/lib/libstorage.dylib" \
        ./build/storage_module_tests
    fi
  '';

  doCheck = true;

  checkPhase = ''
    runHook preCheck
    export QT_QPA_PLATFORM=offscreen
    ctest --test-dir ./build --output-on-failure -V
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp ./build/storage_module_tests $out/bin/
    runHook postInstall
  '';
}
