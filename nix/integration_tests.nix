# Runs the shell-based integration tests for logos-storage-module via logoscore.
# Tests vital operations: start, peerId, upload, download, stop.
{ pkgs, common, src, storageModuleLib, logoscorePkg }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-integration-tests";
  version = common.version;

  inherit src;

  nativeBuildInputs = [ pkgs.bash ];

  dontBuild = true;
  doCheck = true;

  checkPhase = ''
    runHook preCheck
    bash ${../tests/run_tests.sh} \
        ${logoscorePkg}/bin/logoscore \
        ${storageModuleLib} \
        2>&1 | tee test-results.txt
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp test-results.txt $out/
    runHook postInstall
  '';
}
