#!/usr/bin/env bash
#
# Local preview of the versioned docs site: Sphinx HTML + the doc-test report,
# assembled into a gh-pages-like tree so the version switcher and the root
# redirect work offline (served at http://localhost:8000).
#
# The doc-test report needs a full Nix build, so it is cached at $REPORT_CACHE
# (outside docs/_build, which `make clean` wipes) and only regenerated when the
# cache is missing or when you pass --doctest.
#
#   ./docs/preview.sh            # build docs, reuse cached report
#   ./docs/preview.sh --doctest  # regenerate the report first (slow)
#
set -euo pipefail
cd "$(dirname "$0")/.."

REPORT_CACHE="${REPORT_CACHE:-/tmp/storage-doctest-report.html}"

if [ "${1:-}" = "--doctest" ] || [ ! -f "$REPORT_CACHE" ]; then
  echo "==> Generating doc-test report (Nix build, slow)…"
  nix run github:logos-co/logos-doctest -- run \
    doctests/storage-module-runtime.test.yaml \
    --verbose --continue-on-fail \
    --release-for "logos-storage-module=" \
    --report "$REPORT_CACHE"
fi

echo "==> Building docs (clean)…"
make -C docs clean
# Root-relative switcher URL so the dropdown fetch is same-origin locally,
# whatever host you browse from (localhost / 127.0.0.1 / 0.0.0.0).
SWITCHER_JSON_URL=/switcher.json make -C docs html

echo "==> Placing the doc-test report…"
mkdir -p docs/_build/html/doctest
if [ -f "$REPORT_CACHE" ]; then
  cp "$REPORT_CACHE" docs/_build/html/doctest/index.html
else
  cp docs/_root/doctest-missing.html docs/_build/html/doctest/index.html
fi

echo "==> Assembling gh-pages-like tree in ./site…"
rm -rf site && mkdir site
cp -r docs/_build/html site/v0.3.2
cp -r docs/_build/html site/latest
cp docs/_root/index.html site/index.html
cat > site/switcher.json <<'JSON'
[
  { "version": "0.3.2", "url": "/v0.3.2/", "preferred": true },
  { "version": "0.3.1", "url": "/latest/" }
]
JSON

echo "==> Serving on http://localhost:8000  (Ctrl-C to stop)"
python3 -m http.server -d site 8000
