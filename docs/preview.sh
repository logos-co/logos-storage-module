#!/usr/bin/env bash
#
# Local preview of the standalone doc-test reports.
#
# The docs site now lives in the Docusaurus repo `logos-storage/logos-storage-docs`
# (the API reference is generated from `src/storage_module_plugin.h` via
# `docs/sources.json`), so this script only runs the doc-test specs locally:
#
#   ./docs/preview.sh                  # run the runtime doc-test (Nix, slow)
#   ./docs/preview.sh --doctest        # run the runtime doc-test (Nix, slow)
#   ./docs/preview.sh --doctest-ui     # run the storage-ui-app doc-test
#   ./docs/preview.sh --doctest-mix    # run the mix doc-test
#
set -euo pipefail
cd "$(dirname "$0")/.."

case "${1:-}" in
  --doctest-ui)  SPEC=storage-ui-app.test.yaml ;;
  --doctest-mix) SPEC=storage-module-mix.test.yaml ;;
  *)             SPEC=storage-module-runtime.test.yaml ;;
esac

OUTPUT_DIR="./doctests/preview-outputs"
REPORT_CACHE="${REPORT_CACHE:-$OUTPUT_DIR/report.html}"
COMMIT="$(git rev-parse HEAD)"

# nix fetches this commit from the GitHub remote, so HEAD must be pushed. Fail
# fast with guidance rather than after a slow build that ends in a 404.
if [ -z "$(git branch -r --contains "$COMMIT" 2>/dev/null)" ]; then
  echo "ERROR: HEAD ($COMMIT) is not pushed to any remote branch." >&2
  echo "  Push your branch first (any branch, not just master), then re-run." >&2
  exit 1
fi

# Prior runs copy nix-store artefacts in read-only, so restore write perms
# before clearing (same dance as doctests/run.sh).
if [ -e "$OUTPUT_DIR" ]; then chmod -R u+w "$OUTPUT_DIR" 2>/dev/null || true; fi
rm -rf "$OUTPUT_DIR" && mkdir -p "$OUTPUT_DIR"

echo "==> Running $SPEC, keeping artefacts in $OUTPUT_DIR…"
nix run github:logos-co/logos-doctest -- run \
  "doctests/$SPEC" \
  --verbose --continue-on-fail --output-dir "$OUTPUT_DIR" \
  --release-for "logos-storage-module=${COMMIT}" \
  --report "$REPORT_CACHE"
echo "==> Report:           $REPORT_CACHE"
echo "==> Logs & artefacts: $OUTPUT_DIR  (daemon log: $OUTPUT_DIR/logs.txt)"
