# logos-storage-module

## How to Build

### Using Nix (Recommended)

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

The compiled artifacts can be found at `result/`

#### SELinux

If you are using Linux with SELinux enabled, you will not be able to install Nix without disabling it. A common workaround is to install Nix inside a Toolbox container. In that case, if you are using Qt Creator, you may also need to configure the project using submodules.

#### Modular Architecture

The nix build system is organized into modular files in the `/nix` directory:
- `nix/default.nix` - Common configuration (dependencies, flags, metadata)
- `nix/lib.nix` - Module plugin and libstorage library compilation
- `nix/include.nix` - Header generation using logos-cpp-generator

### Using submodules

CMake is also configured to work with submodules. This is particularly useful for proper integration with Qt Creator. You only need to fetch the submodules using:

```bash
git submodule update --init --recursive
```

Everything should work straightforwardly. The submodules are also used as a fallback when the dependency folders are not found on the system. It can also be forced by enabling the `LOGOS_STORAGE_MODULE_USE_VENDOR` option.

Note: While this setup is convenient for integration with Qt Creator, it is strongly recommended to use Nix for producing reproducible and deterministic builds.

### Using local dependencies

Another way to build the project is to clone the dependencies into the same parent directory, for example:

```
logos-storage-module
logos-cpp-sdk
logos-liblogos
logos-storage-ui
```

While this setup is less common, it is also supported and works correctly in Qt Creator

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

Qt Creator provides a great development experience for Qt. To ensure proper integration, it is recommended to either configure the project using submodules or clone the dependencies independently into the same parent directory. Nix should then be used to build the executable.

### Installation

#### Install from the repository (recommended)

If your package manager provides `qtcreator`, this is the easiest way to start. You will need to install some dependencies with it.  
Note that you should install and run it from a Toolbox, otherwise you may face `glx` errors:

```bash
sudo dnf install cmake ninja clangd qtcreator gcc
```

#### Install from the installer

An alternative is to use the [Qt installer](https://www.qt.io/development/download-qt-installer).

Ensure that you already have the build tools installed (see the previous section), or let the installer install them for you (default behavior).

### Configuration

To import the project into Qt Creator, click on `File -> Open File or Project` and select the `CMakeLists.txt` file. A configuration popup will appear. Make sure you have a **Debug** build configuration pointing to the `build` directory and then click on `Configure project`.

Ensure that `clangd` is enabled for your project. Go to `Projects` on the left, then click on `Manage Kits` at the top. Select the `C++` tab and open the last tab, `Clangd`. Check `Use clangd` and, if needed, configure it to use the `clangd` installed on your system.

That’s it. The configuration defined in `CMakeLists.txt` should allow the project to build correctly.

If you encounter any configuration issues, close Qt Creator, remove the `CMakeLists.txt.user` file, and restart Qt Creator to reconfigure the project.

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
