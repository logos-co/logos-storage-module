# Builds and runs the Qt Test suite for logos-storage-module.
#
# The test project lives in tests/ and is intentionally standalone:
# it only depends on Qt6::Core and Qt6::Test so it can be compiled
# without libstorage or any other Logos native dependency.
{ pkgs, src }:

pkgs.stdenv.mkDerivation {
  pname = "logos-storage-module-tests";
  version = "1.0.0";

  inherit src;

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.qt6.wrapQtAppsNoGuiHook
  ];

  buildInputs = [
    pkgs.qt6.qtbase
  ];

  # Point CMake at the tests/ subdirectory, which is a standalone project.
  configurePhase = ''
    runHook preConfigure
    cmake -GNinja -S./tests -B./build
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

    # Qt needs a platform plugin even for headless/Core-only tests.
    export QT_QPA_PLATFORM=offscreen

    cd ./build && ctest --output-on-failure

    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out
    # Keep the binary around so CI can inspect it if needed.
    cp ./build/storage_module_tests $out/
    touch $out/tests-passed

    runHook postInstall
  '';

  meta = {
    description = "Unit tests for logos-storage-module (Qt Test)";
  };
}
