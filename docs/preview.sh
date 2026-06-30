#!/usr/bin/env bash
#
# Local preview of the versioned docs site, or of the standalone doc-test report.
#
# The doc-test report is no longer embedded in the site (it now lives on the
# external doctest hub), so the two are previewed independently:
#
#   ./docs/preview.sh                  # build and serve the docs site (http://localhost:8000)
#   ./docs/preview.sh --doctest        # run the runtime doc-test (Nix, slow)
#   ./docs/preview.sh --doctest-ui     # run the storage-ui-app doc-test
#   ./docs/preview.sh --doctest-mix    # run the mix doc-test
#
#
set -euo pipefail
cd "$(dirname "$0")/.."

case "${1:-}" in
  --doctest)     SPEC=storage-module-runtime.test.yaml ;;
  --doctest-ui)  SPEC=storage-ui-app.test.yaml ;;
  --doctest-mix) SPEC=storage-module-mix.test.yaml ;;
  *)             SPEC="" ;;
esac

if [ -n "$SPEC" ]; then
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
  exit 0
fi

echo "==> Building docs (clean)…"
make -C docs clean
# Root-relative switcher URL so the dropdown fetch is same-origin locally,
# whatever host you browse from (localhost / 127.0.0.1 / 0.0.0.0).
SWITCHER_JSON_URL=/switcher.json make -C docs html

echo "==> Assembling gh-pages-like tree in ./docs/site…"
rm -rf docs/site && mkdir docs/site
cp -r docs/_build/html docs/site/v0.3.2
cp -r docs/_build/html docs/site/latest
cp docs/_root/index.html docs/site/index.html
cp docs/_root/switcher.json docs/site/switcher.json

echo "==> Serving on http://localhost:8000  (Ctrl-C to stop)"
python3 -m http.server -d docs/site 8000
