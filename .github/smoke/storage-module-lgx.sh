#!/usr/bin/env bash
# Windows smoke test for the storage_module .lgx.
#
# Runs from the staged tree ROOT, so paths start with the target name.
#
# There is no PE to launch here -- this repo's Windows artifact is an archive --
# so this script asserts on the PAYLOAD rather than on behaviour, and the `run`
# wrapper is deliberately unused. The behavioural half is the harness's own
# lgx-variant gate, which opens the archive and checks every PE inside it,
# import closures included.
#
# Hand-runnable against any built .lgx, which is how it was developed:
#     LGXDIR=result bash .github/smoke/storage-module-lgx.sh
set -euo pipefail
LGXDIR="${LGXDIR:-lgx-portable}"

# tar is the one tool this needs that neither the repo nor the harness ships.
# Assert it up front: absent, `tar -tzf` fails midway with a message about the
# archive, which reads as a packaging bug rather than a missing tool.
command -v tar >/dev/null || {
  echo "::error::tar is not on PATH, so the archive-layout assertions cannot run."
  echo "::error::This is a runner-image fact, not a defect in the package."
  exit 1
}

lgxf=$(ls "$LGXDIR"/*.lgx) || { echo "::error::no .lgx staged under $LGXDIR"; exit 1; }
echo "asserting on $lgxf"

# CAPTURE, THEN TEST. Piping a listing straight into a quiet matcher is
# forbidden here: the matcher exits at its first hit while tar is still
# listing, tar dies of SIGPIPE (141), and pipefail promotes that to the
# pipeline status -- a false red on a correct archive, or, as an if-condition,
# a silent pass on a bad one. Both shipped in this harness's own gate once.
names=$(tar tzf "$lgxf")

grep -q '^manifest.json$' <<<"$names" \
  || { echo "::error::no manifest.json in the archive"; exit 1; }

# The variant name is the contract with lgpm: it refuses a variant it cannot
# run, and `windows-x86_64-dev` (what the non-portable `lgx` target emits) is
# not installable into a portable bundle.
grep -q '^variants/windows-x86_64/' <<<"$names" \
  || { echo "::error::no variants/windows-x86_64/ payload"; printf '%s\n' "$names" | sed -n '1,20p'; exit 1; }

# No other platform's payload rides along. Checked because `lgx add` writes
# variants in the order they were added, so a stray one is invisible to a
# listing that stops at the first match.
if grep -qE '^variants/(linux|darwin)' <<<"$names"; then
  echo "::error::a non-Windows variant is present in a Windows-only package:"
  grep -E '^variants/(linux|darwin)' <<<"$names" | sed -n '1,10p'
  exit 1
fi

# The module's OWN external library must ride along. storage_module_plugin.dll
# resolves libstorage.dll by BARE NAME and the Windows loader searches the
# calling image's directory, so it has to sit in the same variant directory.
# Absent, the plugin loads on a developer machine (where the DLL is elsewhere
# on PATH) and fails on a clean one with 0xC0000135 and no message.
for want in storage_module_plugin.dll libstorage.dll; do
  grep -q "^variants/windows-x86_64/${want}$" <<<"$names" \
    || { echo "::error::${want} missing from the windows-x86_64 payload"; exit 1; }
done

# And the HOST-provided runtime must NOT ride along -- asserted because the
# portable payload is built by a deliberate strip, and a strip that silently
# stopped working is invisible otherwise. A module carrying its own copy of the
# C++ runtime or of Qt is the duplicate-runtime hazard this project has already
# paid for once: PE has no symbol interposition, so a second copy means a second
# set of function-local statics in the process, and cross-module calls start
# being refused with nothing crashing and nothing logged.
#
# Measured against a real portable payload: it carries exactly
# storage_module_plugin.dll, libstorage.dll and libwinpthread-1.dll.
for unwanted in libstdc++-6.dll libgcc_s_seh-1.dll Qt6Core.dll libcrypto-3-x64.dll; do
  if grep -q "^variants/windows-x86_64/${unwanted}$" <<<"$names"; then
    echo "::error::${unwanted} is in the payload -- the host already ships it."
    echo "::error::A module carrying its own copy gives the process two of every"
    echo "::error::function-local static in that library."
    exit 1
  fi
done

echo "storage_module .lgx smoke: OK"
