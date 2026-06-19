#!/usr/bin/env bash
#
# Local preview of the versioned docs site, or of the standalone doc-test report.
#
# The doc-test report is no longer embedded in the site (it now lives on the
# external doctest hub), so the two are previewed independently:
#
#   ./docs/preview.sh            # build and serve the docs site (http://localhost:8000)
#   ./docs/preview.sh --doctest  # generate the doc-test report only (Nix build, slow)
#
# --doctest writes the report to $REPORT_CACHE; open that file directly to view it.
#
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "${1:-}" = "--doctest" ]; then
  REPORT_CACHE="${REPORT_CACHE:-/tmp/storage-doctest-report.html}"

  echo "==> Generating doc-test report (Nix build, slow)…"
  nix run github:logos-co/logos-doctest -- run \
    doctests/storage-module-runtime.test.yaml \
    --verbose --continue-on-fail \
    --release-for "logos-storage-module=" \
    --report "$REPORT_CACHE"
  echo "==> Report written to $REPORT_CACHE"
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
