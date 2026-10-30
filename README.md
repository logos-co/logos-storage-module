# logos-storage-module

Storage Module is the C++ wrapper of [libstorage](https://github.com/logos-storage/logos-storage-nim/tree/master/library).

## How to Build

### Using Nix

#### Build Complete Module (Library + Headers)

```bash
# Build everything (default)
nix build

# Or explicitly
nix build '.#default'

# Using a local version of logos storage nim
nix build --override-input logos-storage "git+file:///absolute/path/to/logos-storage-nim?submodules=1"
```

The result will include:

- `/lib/storage_module_plugin.dylib` (or `.so` on Linux) - The Storage module plugin

#### Build Individual Components

```bash
# Build only the library (plugin + libstorage)
nix build '.#lib'

# Build only the generated headers
nix build '.#include'
```

#### Development Shell

```bash
# Enter development shell with all dependencies
nix develop
```

**Note:** In zsh, you need to quote the target (e.g., `'.#default'`) to prevent glob expansion.

If you don't have flakes enabled globally, add experimental flags:

```bash
nix build --extra-experimental-features 'nix-command flakes'
```

To enable globally so you don't need these flag for each command, add the following to `~/.config/nix/nix.conf` (create if it doesn't exist):

```ini
experimental-features = nix-command flakes
```

The compiled artifacts can be found at `result/`

## Tests

```bash
# Run all tests
nix run .#tests

# Run only the test binaries whose name matches a pattern
nix run .#tests -- integration

# Build tests
nix build .#tests

# Rerun only unit tests
./result/bin/storage_module_tests

# Rerun only integration tests
./result/bin/storage_module_integration_tests
```

### `doctest`

The `doctest` job runs a full Nix build and generates the `doctest` report on every pull request and on push to `master`.
It ensures that the tutorial examples compile and run correctly.

To run a preview:

```bash
# Generate the doc-test report (each flag runs a different spec)
./docs/preview.sh --doctest        # runtime spec (storage-module-runtime.test.yaml)
./docs/preview.sh --doctest-mix    # mix spec (storage-module-mix.test.yaml)
./docs/preview.sh --doctest-config # config spec (storage-module-config.test.yaml)
```

The report is generated in a temporary file.

The doc-test builds your current commit fetched from GitHub, so **you must push
your branch first** (any branch, not just `master`): otherwise the build can't
find the commit and fails with a 404.

Logos Storage Module's GitHub Pages does not serve the `doctest` report directly. The docs navbar **Tutorial** link points to `https://logos-co.github.io/logos-doctest-hub/#logos-storage-module/ubuntu-latest/running-this-storage-module-against-logoscore`, which embeds the report from the `main` folder published by the `doctest` job on push to `master`.

The link is created in [logos doctest hub](https://github.com/logos-co/logos-doctest-hub/blob/master/repos.json): the tutorial link has to match
the title in the json file.

## Usage

Storage Module has different usages:

1. Run Storage over logos core (see [docs](https://docs.logos.co/storage/get-started/run-logos-storage-node),
   [`docs/logoscore-overview/logoscore.md`](docs/logoscore-overview/logoscore.md)
   and the `doctest` report on the [test hub](https://logos-co.github.io/logos-doctest-hub/#logos-storage-module/ubuntu-latest/running-this-storage-module-against-logoscore)).
2. Run Storage UI (see [repo](https://github.com/logos-co/logos-storage-ui)).
3. Download logos packages (see [repo](https://github.com/logos-co/logos-package-downloader-module)).

## Documentation

Sphinx documentation is built using Doxygen and Breathe.
A new version is deployed on each Github Release and available using
github pages: `https://logos-co.github.io/logos-storage-module/latest`.

To run a preview:

```bash
# Build the docs and serve at http://localhost:8000
./docs/preview.sh
```

### Documentation Requirements

- Python 3 and dependencies: `pip install -r docs/requirements.txt`
- Doxygen
- make

### Publishing a new version

Each **published GitHub Release** deploys a copy of the docs under
`https://logos-co.github.io/logos-storage-module/<tag>/`, refreshes `/latest/`,
and updates the root redirect. The version dropdown is driven by the
hand-maintained `docs/_root/switcher.json`.

To cut a new version (e.g. `v0.4.0`):

1. Add it to `docs/_root/switcher.json`, newest first, and move `"preferred": true`
   onto it. The `"version"` field is the tag **without** the leading `v`:

   ```json
   [
     { "version": "0.4.0", "url": "https://logos-co.github.io/logos-storage-module/v0.4.0/", "preferred": true },
     { "version": "0.3.2", "url": "https://logos-co.github.io/logos-storage-module/v0.3.2/" }
   ]
   ```

2. Commit that change to `master`.

3. Create and publish the Release on that commit — this is what triggers the
   deploy (pushing a bare tag does **not**):

The `Docs` workflow then builds and publishes `v0.4.0/` and `latest/`. GitHub
Pages must be set to deploy from the `gh-pages` branch, `/ (root)`.

You can also deploy **without** publishing a release
with a manual run of the `Docs` workflow and its `deploy` flag on. It publishes
the **latest tag** of the chosen branch:

From the Actions tab in Github: **Docs → Run workflow → check "Force deploy to GitHub
Pages"**.

## Mix relays per network

A Mix configuration per network is available in `mix-config.json`.
The file is generated:

```bash
./tools/gen-mix-config.sh
```

It fetches the data from `fleets.logos.co`, through the `storage-config.sh`
script of `logos-storage-nim` at the revision pinned in `flake.lock`.

The `mix-config.json` CI workflow regenerates it on every PR and fails when the
committed Mix config no longer matches the live one.

## Modular Architecture

The build system is handled by `logos-module-builder`. This module uses the **universal** interface (`"interface": "universal"` in `metadata.json`), which means any glue is auto-generated at build time from `src/storage_module_plugin.h` (via `codegen.impl_header` in `metadata.json`) by `logos-cpp-generator`.

## Output Structure

The plugin and libstorage must stay in the same directory: `storage_module_plugin.dylib` is linked with `@loader_path` to find `libstorage.dylib` beside it.
