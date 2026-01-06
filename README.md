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
nix build '.#logos-storage-module-lib'

# Build only the generated headers
nix build '.#logos-storage-module-include'
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

### Requirements

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