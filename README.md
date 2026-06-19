# logos-storage-module

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
# Run all tests (builds and runs checks defined in flake.nix)
nix flake check

# Run all tests and see the output
nix run .#tests

# Run only test binaries matching a filter
nix run .#tests -- integration
```

### Logoscore

Logoscore can be run from any directory, not just the Storage Module root. Let's call this folder `logoscore-dir`.

First, retrieve liblogos:

```bash
cd logoscore-dir
nix build 'github:logos-co/logos-logoscore-cli' --out-link ./logos
```

Create the modules directory:

```bash
mkdir modules
```

Install the package manager: 

```bash
nix --extra-experimental-features "nix-command flakes" build github:logos-co/logos-package-manager#cli --out-link ./package-manager
```

Now build the lgx package in the logos-storage-module directory:

```bash
cd /path/to/logos-storage-module
nix build '.#lgx'
```

Then go back to your `logoscore-dir` folder and install the lgx package:

```bash
cd logoscore-dir
./package-manager/bin/lgpm --modules-dir ./modules install --dir /path/to/logos-storage-module/result/
```

Get the configuration file, either from the repository or use a local copy:

```bash
# Download from repository
wget https://raw.githubusercontent.com/logos-co/node-configs/refs/heads/master/storage_config.json

# Or copy local file
cp /path/to/config.json .
```

Run logoscore:

```bash
# Start a clean daemon. `-D` runs in the foreground, so background it with `&`
# (`logoscore stop` below shuts it down).
./logos/bin/logoscore -D -m ./modules &

# Wait until the daemon is accepting commands, then load the module
until ./logos/bin/logoscore status >/dev/null 2>&1; do sleep 0.2; done
./logos/bin/logoscore load-module storage_module

# Initialize from config.json, start the node, and import files
./logos/bin/logoscore call storage_module init @config.json
./logos/bin/logoscore call storage_module start
./logos/bin/logoscore call storage_module importFiles /path/to/import/files

# Expected output
# [info] [storage_module] StorageModuleImpl::importFiles: upload started, session=1
# [info] [storage_module] [LogosProviderObject] emitEvent: "storageUploadProgress"
# [info] [storage_module] [LogosProviderObject] emitEvent: "storageUploadDone"

# Get the list of uploaded files
./logos/bin/logoscore call storage_module manifests

# Stop the daemon when done
./logos/bin/logoscore stop
```

## Documentation

The docs are built with Sphinx (API reference via Doxygen + Breathe) and embed
the runtime doc-test report. `docs/preview.sh` builds the site and assembles a
gh-pages-like tree and serves it:

```bash
# Build the docs and serve at http://localhost:8000
./docs/preview.sh

# Regenerate the doctest report first (slow: full Nix build)
./docs/preview.sh --doctest
```

### Documentation Requirements

- Python 3 and dependencies: `pip install -r docs/requirements.txt`
- Doxygen
- make
- Nix (doctest)

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

## SELinux

If you are using Linux with SELinux enabled, you will not be able to install Nix without disabling it. A common workaround is to install Nix inside a Toolbox container.

## Modular Architecture

The build system is handled by `logos-module-builder`. This module uses the **universal** interface (`"interface": "universal"` in `metadata.json`), which means any glue is auto-generated at build time from `src/storage_module_plugin.h` (via `codegen.impl_header` in `metadata.json`) by `logos-cpp-generator`.

## Output Structure

When built with Nix, the module produces:

```
result/
└── lib/
    └── storage_module_plugin.dylib  # Logos module plugin
```

Both libraries must remain in the same directory, as `storage_module_plugin.dylib` is configured with `@loader_path` to find `libstorage.dylib` relative to itself.

## Qt Creator Setup

See [qt.md](docs/qt.md) for instructions on setting up Qt Creator.

## Requirements

### Build Tools

- CMake (3.14 or later)
- Ninja build system
- pkg-config

### Dependencies

- logos-module-builder (build system + code generator)
- logos-liblogos
- nlohmann_json
- [libstorage](https://github.com/logos-storage/logos-storage-nim/tree/chore/improve-c-bindings/library)
