# Builds the logos-storage-module library
{ pkgs, common, src, logosStorageNim }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-lib";
  version = common.version;

  inherit src;
  inherit (common) nativeBuildInputs buildInputs cmakeFlags meta env;

  # Determine platform-specific library extension
  libstorageLib = if pkgs.stdenv.hostPlatform.isDarwin then "libstorage.dylib" else "libstorage.so";

  postInstall = ''
    mkdir -p $out/lib

    # Copy the lib to $out/lib folder
    if [ -f "$out/share/logos-storage-module/generated/''${libstorageLib}" ]; then
        cp "$out/share/logos-storage-module/generated/''${libstorageLib}" "$out/lib/''${libstorageLib}"
    fi

    # Fix the install name of libstorage on macOS
    ${pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
      ${pkgs.darwin.cctools}/bin/install_name_tool -id "@rpath/''${libstorageLib}" "$out/lib/''${libstorageLib}"
    ''}

    # Copy the storage module plugin from the installed location
    if [ -f "$out/lib/logos/modules/storage_module_plugin.dylib" ]; then
      cp "$out/lib/logos/modules/storage_module_plugin.dylib" "$out/lib/"

      # Fix the plugin's reference to libstorage on macOS
      ${pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
        # Find what libstorage path the plugin is referencing and change it to @rpath
        for dep in $(${pkgs.darwin.cctools}/bin/otool -L "$out/lib/storage_module_plugin.dylib" | grep libstorage | awk '{print $1}'); do
          ${pkgs.darwin.cctools}/bin/install_name_tool -change "$dep" "@rpath/''${libstorageLib}" "$out/lib/storage_module_plugin.dylib"
        done
      ''}
    elif [ -f "$out/lib/logos/modules/storage_module_plugin.so" ]; then
      cp "$out/lib/logos/modules/storage_module_plugin.so" "$out/lib/"
    else
      echo "Error: No storage_module_plugin library file found"
      exit 1
    fi

    # Remove the nested structure we don't want
    rm -rf "$out/lib/logos" 2>/dev/null || true
    rm -rf "$out/share" 2>/dev/null || true
  '';
}
