# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Logos platform module (C++) that wraps `libstorage`, the storage engine
built from `logos-storage-nim` (sibling repo under `~/Work/logos/`). It
exposes upload/download and data-management over a p2p network to other Logos
modules and to the headless `logoscore` host.

## Architecture (the parts that span files)

- **The public API is one class: `StorageModuleImpl`** in
  `src/storage_module_plugin.h`. This is a *universal* module
  (`"interface": "universal"` in `metadata.json`): the glue layer is
  **auto-generated at build time** from that header by `logos-cpp-generator`
  (`codegen.impl_header` in `metadata.json`). The generator wires the
  `emitEvent` callback and the `on<EventName>` accessors. Consequence: the
  Doxygen comments on `StorageModuleImpl` *are* the API reference (rendered by
  Doxygen → Breathe → Sphinx). Editing method signatures or their JSON
  payloads changes both the ABI and the docs.

- **`StorageModuleImpl` calls `libstorage`** (C API, vendored at
  `lib/libstorage.h`; source of truth is `logos-storage-nim/library/libstorage.h`).
  libstorage functions are asynchronous: a command is dispatched to a worker
  thread and the result arrives via a `StorageCallback`. The return code is
  only the synchronous dispatch status (`RET_OK`/`RET_ERR`/`RET_PROGRESS`).

- **Two callback patterns** in `src/storage_module_plugin.cpp`:
  - *Fire-and-forget, event-emitting* (`AsyncCallbackBase` strategy:
    `SimpleEventCtx`, `ConnectCtx`, `UploadFileCtx`, `UploadChunkCtx`,
    `DownloadStreamCtx`) — used by async ops; emit a named event on completion.
  - *Blocking* (`SyncCtx` + `waitSync`) — used by synchronous getters. The
    `abandoned` flag prevents use-after-free when the caller times out before
    the callback fires; preserve it when touching this code.

- **Return-type convention:** `init()`/`start()` return `bool` (for headless
  compatibility); everything else returns `StdLogosResult`. Async operations
  report their real outcome through the events declared in `metadata.json`
  (`storageStart/Stop/Connect/UploadProgress/UploadDone/DownloadProgress/DownloadDone`).
  These three must stay in sync: the events in `metadata.json`, the strings
  the `.cpp` emits, and the payloads documented in the header.

## Docs

- User-facing guide lives in `docs/index.rst` (Configuration / Connectivity /
  Mix sections). The config **defaults and option names there are mirrored
  from `conf.nim` in `logos-storage-nim`, which is the source of truth** —
  update both together, and verify defaults against `conf.nim` rather than
  trusting the prose. `nat` semantics live in `logos-storage-nim/storage/nat.nim`.
- The API reference page is generated from `src/storage_module_plugin.h`;
  rebuild the docs to see header changes (they are not live).

## Commands

```bash
nix build                 # build the module (plugin + libstorage); also '.#default'
nix build '.#lib'         # library only
nix build '.#include'     # generated headers only
nix build '.#lgx'         # package as an .lgx for logoscore/lgpm
nix develop               # dev shell with all deps

# Build against a local logos-storage-nim checkout:
nix build --override-input logos-storage "git+file:///abs/path/to/logos-storage-nim?submodules=1"

nix flake check           # run all tests
nix run .#tests           # run tests and show output
nix run .#tests -- <filter>   # run only test binaries matching <filter>

./docs/preview.sh         # build + serve the Sphinx site at localhost:8000
./docs/preview.sh --doctest   # run the doc-test (requires the branch to be pushed first)
```

The CMake build needs `LOGOS_MODULE_BUILDER_ROOT` set (provided by the Nix
build via `logos-module-builder`).

## Packaging note

The built plugin and `libstorage.dylib`/`.so` must stay in the same directory:
`storage_module_plugin.dylib` is linked with `@loader_path` to find libstorage
beside it.
