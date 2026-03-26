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
