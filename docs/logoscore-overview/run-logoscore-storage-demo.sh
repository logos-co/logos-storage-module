#!/usr/bin/env bash

set -euo pipefail

CMD_COLOR="\033[38;5;43m"
INFO_COLOR="\033[38;5;110m"
WARN_COLOR="\033[38;5;179m"
ERROR_COLOR="\033[38;5;203m"
RESET="\033[0m"

info() {
  printf "%b%s%b\n" "$INFO_COLOR" "$*" "$RESET"
}

warn() {
  printf "%b%s%b\n" "$WARN_COLOR" "$*" "$RESET"
}

error() {
  printf "%b%s%b\n" "$ERROR_COLOR" "$*" "$RESET" >&2
}

run_cmd() {
  printf "%b$" "$CMD_COLOR"
  printf " %q" "$@"
  printf "%b\n" "$RESET"
  "$@"
}

require_file() {
  if [[ ! -e "$1" ]]; then
    error "Required path is missing: $1"
    exit 1
  fi
}

remove_dir_prompt() {
  local dir="$1"
  local description="$2"
  local on_keep="$3"

  if [[ ! -e "$dir" ]]; then
    return 0
  fi

  warn "The $description already exists: $dir"
  printf "%bRemove it? [y/N] %b" "$WARN_COLOR" "$RESET"

  local answer=""
  IFS= read -r -n 1 answer || true
  printf "\n"

  case "$answer" in
    y|Y)
      run_cmd rm -rf "$dir"
      ;;
    *)
      info "Keeping existing $description."
      if [[ "$on_keep" == "exit" ]]; then
        info "Exiting without changing files."
        exit 0
      fi
      ;;
  esac
}

module_is_loaded() {
  "$LOGOSCORE" list-modules --loaded 2>/dev/null | grep -Eq "(^|[^[:alnum:]_])$MODULE([^[:alnum:]_]|$)"
}

config_data_dir() {
  local value=""
  value=$(grep -E '"data-dir"[[:space:]]*:' "$CONFIG" | head -n 1 | sed -E 's/.*"data-dir"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/')

  if [[ -z "$value" || "$value" == *'"data-dir"'* ]]; then
    return 1
  fi

  if [[ "$value" = /* ]]; then
    printf "%s\n" "$value"
  else
    printf "%s/%s\n" "$PWD" "$value"
  fi
}

LOGOSCORE="./logos/bin/logoscore"
MODULE="storage_module"
CONFIG="./config.json"
FILES_DIR_ABS="$PWD/files"

info "Logos Storage Module demo helper"
info "Run this script from your logoscore runtime directory, for example: ~/code/tmp/logoscore-dir"
info "Start the daemon first in a separate terminal or tmux pane: ./logos/bin/logoscore -D -m ./modules"
printf "\n"

require_file "$LOGOSCORE"
require_file "$CONFIG"
require_file "./modules"

info "Waiting for the logoscore daemon to accept commands..."
until "$LOGOSCORE" status >/dev/null 2>&1; do
  sleep 0.2
done

info "Daemon is responding. Current status:"
run_cmd "$LOGOSCORE" status
printf "\n"

info "Checking whether $MODULE is already loaded in this daemon session."
if module_is_loaded; then
  error "$MODULE is already loaded. Not touching storage data while the module may be using it."
  error "Stop the daemon, start a fresh daemon, then run this script again."
  exit 1
fi
info "$MODULE is not loaded yet; storage-data cleanup is safe to offer."
printf "\n"

DATA_DIR=""
if DATA_DIR=$(config_data_dir); then
  info "Configured storage data directory: $DATA_DIR"
  remove_dir_prompt "$DATA_DIR" "storage data directory" "continue"
  printf "\n"
else
  warn "Could not read data-dir from $CONFIG. Skipping storage data cleanup prompt."
  warn "The init command will still use the full config file."
  printf "\n"
fi

info "Loading the storage module. If it is already loaded, this command may report that state."
run_cmd "$LOGOSCORE" load-module "$MODULE"
printf "\n"

info "Initializing the storage node from config.json. This is a synchronous module call."
run_cmd "$LOGOSCORE" call "$MODULE" init "@$CONFIG"
printf "\n"

info "Starting the storage node. The command returns dispatch status; watch the daemon terminal for storageStart/log output."
run_cmd "$LOGOSCORE" call "$MODULE" start
printf "\n"

info "Giving the node a brief moment to finish startup before importing files."
sleep 2

remove_dir_prompt "$FILES_DIR_ABS" "generated sample files directory" "exit"
run_cmd mkdir -p "$FILES_DIR_ABS"

info "Creating three random sample files for import."
run_cmd dd if=/dev/urandom of="$FILES_DIR_ABS/data1k.bin" bs=1024 count=1 status=none
run_cmd dd if=/dev/urandom of="$FILES_DIR_ABS/data1M.bin" bs=1M count=1 status=none
run_cmd dd if=/dev/urandom of="$FILES_DIR_ABS/data10M.bin" bs=10M count=1 status=none
printf "\n"

info "Importing files from $FILES_DIR_ABS."
info "Expected daemon-side signs of success include storageUploadProgress and storageUploadDone events."
info "The importFiles call starts uploads; completion details are easiest to watch in the daemon terminal."
run_cmd "$LOGOSCORE" call "$MODULE" importFiles "$FILES_DIR_ABS"
printf "\n"

info "Waiting briefly for upload callbacks to complete before listing manifests."
sleep 5

info "Listing manifests stored locally after import."
run_cmd "$LOGOSCORE" call "$MODULE" manifests
printf "\n"

info "Stopping the logoscore daemon."
run_cmd "$LOGOSCORE" stop
printf "\n"

info "Removing generated sample files."
run_cmd rm -rf "$FILES_DIR_ABS"
printf "\n"

info "Demo helper completed."
