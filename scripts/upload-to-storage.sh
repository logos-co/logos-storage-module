#!/usr/bin/env bash
#
# Uploads one file into Logos Storage and prints its CID.
#
# The script is meant to be used as restricted command in the seeder's authorized_keys file, e.g.:
#
#   restrict,command="/opt/upload-to-storage.sh" ssh-ed25519 AAAA...
#
# Whenever the SSH client connects with that key, the command is run and the file is read from standard input.
#
# Example usage:
#
#   cid=$(ssh lgx@seeder storage_module-2.0.1.lgx < storage_module-2.0.1.lgx)
#
# SSH_ORIGINAL_COMMAND is the file name and the file itself is read from standard input.
#
# Requires a running logosctl daemon on the seeder with storage_module loaded.

# Exit on error, unset variable, or pipe failure.
set -euo pipefail

readonly CHUNK_SIZE=65536

# The package name is passed as the SSH_ORIGINAL_COMMAND environment variable.
# SSH_ORIGINAL_COMMAND is set by the SSH server when a restricted command is used in authorized_keys.
name=${SSH_ORIGINAL_COMMAND:-}
if [ -z "$name" ]; then
  echo "missing package name" >&2
  exit 1
fi

# Get the upload session ID from the storage module.
session=$(logosctl -j call storage_module uploadInit "$name" "$CHUNK_SIZE" \
  | jq -er '.result.value')

# Cancel the upload on error.
trap 'logosctl -j call storage_module uploadCancel "str:$session" >/dev/null 2>&1' ERR

while true; do
  # logosctl expects base64url-encoded chunks, without padding.
  # See https://github.com/logos-co/logos-logoscore-cli/blob/1a4f56fe2b5fe3ed34d7063a4b7376627c8cb838/docs/logosctl.md?plain=1#L382-L384.
  chunk=$(head -c "$CHUNK_SIZE" | basenc --base64url -w0 | tr -d '=')

  # Ensute this is not empty.
  if [ -z "$chunk" ]; then
    break
  fi

  # logosctl expects `{"_bytes": base64url}`.
  # See https://github.com/logos-co/logos-logoscore-cli/blob/1a4f56fe2b5fe3ed34d7063a4b7376627c8cb838/docs/logosctl.md?plain=1#L382-L384
  logosctl -j call storage_module uploadChunk "str:$session" \
    "json:{\"_bytes\":\"$chunk\"}" | jq -e '.result.success' >/dev/null
done

logosctl -j call storage_module uploadFinalize "str:$session" | jq -er '.result.value'
