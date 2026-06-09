# logos-storage-module

## How to Build

### Using Nix

#### Build Complete Module (Library + Headers)

```bash
# Build everything (default)
nix build

# Or explicitly
nix build '.#default'
```

The result will include:
- `/lib/storage_module_plugin.dylib` (or `.so` on Linux) - The Storage module plugin
- `/include/storage_module_api.h` - Generated header for the module API
- `/include/storage_module_api.cpp` - Generated implementation for the module API

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

#### Headless mode

The headless mode can be run from any directory, not just the Storage Module root. The following commands assume you're in the directory where you want to run the headless mode.

First, retrieve lib-logos:

```bash
nix --extra-experimental-features "nix-command flakes" build github:logos-co/logos-liblogos --out-link ./logos
```

Create the modules directory:

```bash
mkdir modules
```

Now retrieve the library files. You have two options:

Using the package manager:

```bash
nix --extra-experimental-features "nix-command flakes" build github:logos-co/logos-package-manager-module#cli --out-link ./package-manager
./package-manager/bin/lgpm --modules-dir ./modules/ install logos-storage-module
```

Or copy from a local build:

```bash
cp /path/to/logos-storage-module/result/lib/* modules/
```

Get the configuration file, either from the repository or use a local copy:

```bash
# Download from repository
wget https://raw.githubusercontent.com/logos-co/node-configs/refs/heads/master/storage_config.json

# Or copy local file
cp /path/to/config.json .
```

Run the headless mode:

```bash
# Start a daemon with storage_module loaded. `-D` runs in the foreground, so
# background it with `&` (`logoscore stop` below shuts it down).
./logos/bin/logoscore -D -m ./modules --load-modules storage_module &

# Wait until the daemon is accepting commands
until ./logos/bin/logoscore status >/dev/null 2>&1; do sleep 0.2; done

# Initialize from config.json, start the node, and import files
./logos/bin/logoscore call storage_module init @config.json
./logos/bin/logoscore call storage_module start
./logos/bin/logoscore call storage_module importFiles /path/to/import/files

# Stop the daemon when done
./logos/bin/logoscore stop
```

These calls initialize the module from `config.json`, start the node, and import files from the specified directory.

You should see logs similar to:

```
Debug: [LOGOS_HOST "storage_module" ]: "LogosAPIClient: Received event: \"storageUploadDone\""
Debug: [LOGOS_HOST "storage_module" ]: "LogosAPIClient: Emitting event: \"storageUploadDone\""
Debug: [LOGOS_HOST "storage_module" ]: "File \"CMakeLists.txt\" uploaded successfully, session: \"0\" cid= \"zDvZRwzkyHVgr59zFkX7vyfzK7oUP7Jc6k7qpFD9ssDi7V5fvdjw\""
Debug: [LOGOS_HOST "storage_module" ]: "importFiles completed: 1 / 1 files uploaded"
```

#### SELinux

If you are using Linux with SELinux enabled, you will not be able to install Nix without disabling it. A common workaround is to install Nix inside a Toolbox container.

#### Modular Architecture

The nix build system is organized into modular files in the `/nix` directory:
- `nix/default.nix` - Common configuration (dependencies, flags, metadata)
- `nix/lib.nix` - Module plugin and libstorage library compilation
- `nix/include.nix` - Header generation using logos-cpp-generator

## Output Structure

When built with Nix, the module produces:

```
result/
├── lib/
│   └── storage_module_plugin.dylib  # Logos module plugin
└── include/
    ├── storage_module_api.h      # Generated API header
    └── storage_module_api.cpp    # Generated API implementation
```

Both libraries must remain in the same directory, as `storage_module_plugin.dylib` is configured with `@loader_path` to find `libstorage.dylib` relative to itself.

## Qt Creator (for development)

Qt Creator provides a great development experience for Qt. To ensure proper integration and setup of environment variables, qtcreator must be launched from a Nix development shell: 
```bash
# enter nix development shell
nix develop

# launch qt creator
# on macos, this is typically located at "/Applications/Qt\ Creator.app/Contents/MacOS/Qt\ Creator"
path/to/qtcreator_exec

### Installation

#### Install from the repository

If your package manager provides `qtcreator`, this is the easiest way to start. You will need to install some dependencies with it.  
Note that you should install and run it from a Toolbox, otherwise you may face `glx` errors:

```bash
sudo dnf install cmake ninja clangd qtcreator gcc
```

If you cannot run it from inside a toolbox, try to install `chromium` in order to have proper dependencies installed.

### Configuration

To import the project into Qt Creator, click on `File -> Open File or Project` and select the `CMakeLists.txt` file. A configuration popup will appear. Make sure you have a **Debug** build configuration pointing to the `build` directory and then click on `Configure project`.

Enable CMake debug logging, add `--log-level=DEBUG` in `Projects` -> `Imported Kits` -> `Build` -> `Additional CMake options`.

Ensure that `clangd` is enabled for your project. Go to `Projects` on the left, then click on `Manage Kits` at the top. Select the `C++` tab and open the last tab, `Clangd`. Check `Use clangd` and, if needed, configure it to use the `clangd` installed on your system.

That’s it. The configuration defined in `CMakeLists.txt` should allow the project to build correctly.

If you encounter any configuration issues, close Qt Creator, remove the `CMakeLists.txt.user` file, and restart Qt Creator to reconfigure the project.

## Tests

```bash
# Run all tests (silent on success)
nix flake check

# Run all tests and always see the output
nix run .#tests

# Run a single test
nix run .#tests -- test_peerId
```

## Requirements

#### Build Tools
- CMake (3.14 or later)
- Ninja build system
- pkg-config

#### Dependencies
- Qt6 (qtbase)
- Qt6 Remote Objects (qtremoteobjects)
- logos-liblogos
- logos-cpp-sdk (for header generation)
- [libstorage](https://github.com/logos-storage/logos-storage-nim/tree/chore/improve-c-bindings/library)
