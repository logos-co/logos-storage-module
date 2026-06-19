#!/usr/bin/env bash
#
# Local preview of the versioned docs site, or of the standalone doc-test report.
#
# The doc-test report is no longer embedded in the site (it now lives on the
# external doctest hub), so the two are previewed independently:
#
#   ./docs/preview.sh                  # build and serve the docs site (http://localhost:8000)
#   ./docs/preview.sh --doctest        # run the doc-test (Nix, slow), keep logs in ./outputs
#   ./docs/preview.sh --doctest -o dbg # ...keeping logs/artefacts in a chosen dir
#
# --doctest pins the run to the local HEAD (nix fetches it from the remote, so
# HEAD must be pushed; override with COMMIT). It keeps the run workdir (logs.txt,
# cid.txt, downloaded.txt, ...) under the --output dir (default ./outputs) so you
# can inspect the logs, and also writes an HTML report to $REPORT_CACHE.
#
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "${1:-}" = "--doctest" ]; then
  shift
  OUTPUT_DIR="./doctests/outputs"
  if [ "${1:-}" = "-o" ] || [ "${1:-}" = "--output" ]; then
    OUTPUT_DIR="${2:?--output requires a directory}"
  fi
  REPORT_CACHE="${REPORT_CACHE:-/tmp/storage-doctest-report.html}"
  COMMIT="${COMMIT-$(git rev-parse HEAD)}"

  # nix fetches $COMMIT from the GitHub remote, so it must be pushed. Fail fast
  # with guidance rather than after a slow build that ends in a 404.
  if [ -n "$COMMIT" ] && [ -z "$(git branch -r --contains "$COMMIT" 2>/dev/null)" ]; then
    echo "ERROR: commit $COMMIT is not on any remote branch." >&2
    echo "  Push your branch first, or build from master with:" >&2
    echo "      COMMIT= ./docs/preview.sh --doctest" >&2
    exit 1
  fi

  # Prior runs copy nix-store artefacts in read-only, so restore write perms
  # before clearing (same dance as doctests/run.sh).
  if [ -e "$OUTPUT_DIR" ]; then chmod -R u+w "$OUTPUT_DIR" 2>/dev/null || true; fi
  rm -rf "$OUTPUT_DIR" && mkdir -p "$OUTPUT_DIR"

  echo "==> Running doc-test (Nix build, slow); keeping artefacts in $OUTPUT_DIR…"
  nix run github:logos-co/logos-doctest -- run \
    doctests/storage-module-runtime.test.yaml \
    --verbose --continue-on-fail \
    --release-for "logos-storage-module=${COMMIT}" \
    --output-dir "$OUTPUT_DIR" \
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

echo "==> Assembling gh-pages-like tree in ./site…"
rm -rf site && mkdir site
cp -r docs/_build/html site/v0.3.2
cp -r docs/_build/html site/latest
cp docs/_root/index.html site/index.html
cp docs/_root/switcher.json site/switcher.json

echo "==> Serving on http://localhost:8000  (Ctrl-C to stop)"
python3 -m http.server -d site 8000
