# Common build configuration shared across all packages
{ pkgs, logosSdk, logosLiblogos, logosStorageNim }:

{
  pname = "logos-storage-module";
  version = "1.0.0";

  # Common native build inputs
  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.qt6.wrapQtAppsNoGuiHook
  ];

  # Common runtime dependencies
  buildInputs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtremoteobjects
  ];

  # Common CMake flags
  cmakeFlags = [
    "-GNinja"
    "-DLOGOS_CPP_SDK_ROOT=${logosSdk}"
    "-DLOGOS_LIBLOGOS_ROOT=${logosLiblogos}"
    "-DLOGOS_STORAGE_NIM_ROOT=${logosStorageNim}"
    "-DLOGOS_STORAGE_MODULE_USE_VENDOR=OFF"
  ];

  # Environment variables
  env = {
    LOGOS_CPP_SDK_ROOT = "${logosSdk}";
    LOGOS_LIBLOGOS_ROOT = "${logosLiblogos}";
  };

  # Metadata
  meta = with pkgs.lib; {
    description = "Logos Storage Module - Provides storage capabilities";
    platforms = platforms.unix;
  };
}
