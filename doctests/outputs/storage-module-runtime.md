# Running This Storage Module Against logoscore

`logos-storage-module` is a Logos `core` module that wraps the
[libstorage](https://github.com/logos-storage/logos-storage-nim) C library to
run a decentralised storage node — upload, download, and data-management
operations over a libp2p network. This doc-test exercises **this**
storage-module commit end-to-end through the headless `logoscore` runtime:

1. Build the `logoscore` CLI and the `lgpm` local package manager from their
   published flakes. `logoscore` is the headless frontend for `logos-liblogos`,
   so building it brings in the whole module-runtime stack (`logos_host`,
   `liblogos_core`, the IPC layer).
2. Build **this** storage module as an installable `.lgx` package straight from
   its own flake's `#lgx` output, **pinned to the commit under test** — so the
   module you run is built from exactly what is checked out here, not the latest
   published release.
3. Install the `.lgx` into a `./modules` directory with `lgpm`.
4. Start `logoscore` in daemon mode (`-D`), load `storage_module`, introspect
   it with `module-info`, and drive a real node lifecycle: start it from a
   config, read its identity, upload a local file, and
   stop it again — verifying the module actually runs and round-trips real
   values through libstorage.

Because the module is built from the commit under test and then loaded and called
through a real `logoscore` daemon, a green run is real evidence that this change
keeps the storage module loadable and callable.

**What you'll build:** This `storage_module`, packaged as `.lgx`, installed with `lgpm`, and driven through a `logoscore` daemon — node start, a local file upload, and shutdown.

**What you'll learn:**

- How to build the `logoscore` runtime and the `lgpm` package manager from their flakes
- How a module's flake exposes a ready-to-install `.lgx` via its `#lgx` output
- How to install an `.lgx` into a modules directory with `lgpm`
- How to start the `logoscore` daemon, load a module, introspect it, and call its methods
- How to configure, start, exercise, and stop a libstorage node headlessly
- How to shut the daemon down and confirm it has exited

## Prerequisites

- **Nix** with flakes enabled. Install from [nixos.org](https://nixos.org/download.html), then enable flakes:

```bash
mkdir -p ~/.config/nix
echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
```

Verify: `nix flake --help >/dev/null 2>&1 && echo "Flakes enabled"`

- **A Linux or macOS machine.**

---

## Step 1: Build logoscore

Build the `logoscore` CLI from its published flake. The result is symlinked to
`./logos/`. `logoscore` is the headless frontend for `logos-liblogos`, so this
one build brings in the whole module-runtime stack the daemon needs.

### 1.1 Build the CLI

```bash
nix build 'github:logos-co/logos-logoscore-cli' --out-link ./logos
```

The build produces `logos/bin/logoscore` plus bundled runtime libraries
and a `logos/modules/` directory containing the built-in
`capability_module` (required for the auth handshake when loading
modules).

---

## Step 2: Build the lgpm package manager

`lgpm` installs `.lgx` packages into a modules directory and scans what is
installed. Build it from the `logos-package-manager` flake and link it as
`./lgpm`.

### 2.1 Build lgpm

```bash
nix build 'github:logos-co/logos-package-manager#cli' -o lgpm
```

The executable is at `./lgpm/bin/lgpm`.

---

## Step 3: Build and install this storage module

Build **this** storage module's `.lgx` straight from its flake's `#lgx`
output and install it into a local `./modules` directory with `lgpm`. Every
module built with
[`logos-module-builder`](https://github.com/logos-co/logos-module-builder)
exposes a ready-to-install `#lgx`.

> The `` in the URL is what pins the build to a specific commit: the
> doc-test runner expands it to a concrete ref. Locally that is this
> checkout's `HEAD` (see `run.sh`); in CI it is the commit being tested. With
> no pin it falls back to the latest `master`.

### 3.1 Build the module's .lgx

Build the `#lgx` output and link it as `./storage-lgx`. (This compiles
the module and its libstorage dependency through Nix, so the first build
is slow.)

```bash
# From inside the clone this is simply: nix build '.#lgx'
nix build 'github:logos-co/logos-storage-module/f258c7db9c4f1354f401dd68e4d0b48b3a08fd36#lgx' -o storage-lgx
```

The `.lgx` package is now under `./storage-lgx/`:

```bash
ls storage-lgx/*.lgx
```

### 3.2 Seed the modules directory with the bundled capability module

`storage_module` is loaded through the host's capability layer, so the
modules directory also needs the `capability_module` that ships with
`logoscore`. Copy it across first.

```bash
mkdir -p modules
cp -RL ./logos/modules/. ./modules/

```

### 3.3 Install the .lgx with lgpm

Install the freshly-built package into `./modules`. `storage_module` is
a `core` module, so it goes to `--modules-dir`. The package is unsigned
(a local dev build), so we pass `--allow-unsigned`.

```bash
./lgpm/bin/lgpm --modules-dir ./modules --allow-unsigned install --file storage-lgx/*.lgx
```

### 3.4 Confirm the install

Scan the directory and confirm the module landed:

```bash
./lgpm/bin/lgpm --modules-dir ./modules list
```

---

## Step 4: Run the daemon and call the module

Start `logoscore` in daemon mode pointed at `./modules`, then use the client
subcommands to load `storage_module`, introspect it, and drive a node
lifecycle. Daemon output is captured in `logs.txt`.

### 4.1 Start the daemon

Start logoscore in daemon mode in the background, capturing output to
`logs.txt`:

```bash
logoscore -D -m ./modules > logs.txt &
```

The `-D` flag starts the daemon. The client subcommands below connect to
this running process via the config written under `~/.logoscore/`.

```bash
sleep 3
```

### 4.2 Inspect the startup log

Review the daemon's startup output:

```bash
cat logs.txt
```

### 4.3 Check daemon status

Verify the daemon is running:

```bash
logoscore status
```

### 4.4 List discovered modules

`storage_module` should be visible in the scan directory:

```bash
logoscore list-modules
```

### 4.5 Load the module

Load `storage_module` into the running daemon:

```bash
logoscore load-module storage_module
```

### 4.6 Confirm the module is loaded

Re-run `status`; the module that was `not_loaded` before now reports
`loaded`:

```bash
logoscore status
```

### 4.7 Introspect the module with module-info

`module-info` lists the `Q_INVOKABLE` methods the module exposes — the
same methods you can `call`:

```bash
logoscore module-info storage_module
```

### 4.8 Write the node configuration

`storage_module.start` takes a JSON configuration string. We keep it
minimal and let libstorage fill in sensible defaults for everything else
(listen addresses, repo kind, quota, discovery) — that already yields a
fully isolated, working node:

- `data-dir` — the node's on-disk repo. We use an **absolute** path and
  create the directory first, because in daemon mode the module runs as
  its own process whose working directory may differ from this one, and
  libstorage opens the repo at exactly the path given.
- `log-level` / `log-file` — send the node's logs to a file in that dir.

The directory is created and the config written in one step (using
`$(pwd)` so the paths are absolute):

```bash
mkdir -p "$(pwd)/storage-data"
cat > config.json <<EOF
{
    "data-dir": "$(pwd)/storage-data",
    "log-level": "DEBUG",
    "log-file": "$(pwd)/storage-data/storage.log"
}
EOF
```

### 4.9 Start the node

`start` creates and configures a libstorage node from the JSON config,
then brings the libp2p node online.
The `@config.json` syntax loads the file's contents as the argument. It
is asynchronous: the return value only confirms the start command was
accepted; the real outcome is delivered as a `storageStart` event in the
daemon log. The remaining calls all run against this started node.

```bash
logoscore call storage_module start @config.json
```

### 4.10 Wait for the node to come up

Give the node a moment to start, then inspect the log for the `storageStart` event:

```bash
sleep 3
```

```bash
cat logs.txt
```

Look for the emitted `storageStart` event carrying
`{ "success": true, ... }`.

### 4.11 Read the libstorage version

`version` returns the libstorage version string — a real round-trip
through the C library wrapped by the module, dispatched over liblogos'
IPC:

```bash
logoscore call storage_module version
```

### 4.12 Read the node's data directory

`dataDir` returns the path of the node's on-disk repo — the `data-dir`
from the config, resolved by libstorage:

```bash
logoscore call storage_module dataDir
```

### 4.13 Read the node's peer ID

`peerId` returns the node's libp2p
[peer identity](https://docs.libp2p.io/concepts/fundamentals/peers/) —
meaningful only once the node has started:

```bash
logoscore call storage_module peerId
```

### 4.14 Read the node's Signed Peer Record

`spr` returns the node's Signed Peer Record (its self-certified addresses):

```bash
logoscore call storage_module spr
```

### 4.15 Inspect storage space

`space` returns a JSON object describing the node's quota and usage. It
exercises a JSON-object round-trip:

```bash
logoscore call storage_module space
```

### 4.16 List manifests (empty baseline)

`manifests` lists everything stored locally. On a fresh node this is an
empty array — we'll call it again after an upload to see it change:

```bash
logoscore call storage_module manifests
```

### 4.17 Upload a local file

Create a small file and upload it. `uploadUrl` takes an **absolute** path
(the daemon resolves it from its own working directory) and a chunk size
in bytes, and returns a session ID immediately; the upload itself runs in
the background. On a fresh `fs` node with no peers the blocks are stored
locally, so this is a real, fully-offline round-trip.

```
Hello from the logos-storage-module doc-test.
```

```bash
logoscore call storage_module uploadUrl "$(pwd)/hello.txt" 65536
```

### 4.18 Wait for the upload to complete

Give the upload a moment, then inspect the log for the `storageUploadDone` event and its CID:

```bash
sleep 3
```

```bash
cat logs.txt
```

Look for the emitted `storageUploadDone` event carrying the new content's
`cid` — proof the file was chunked, stored, and a manifest written.

### 4.19 List manifests (now populated)

Call `manifests` again. The uploaded file now appears as a stored
manifest — we assert on presence rather than the (non-deterministic) CID:

```bash
logoscore call storage_module manifests
```

### 4.20 Stop the node

`stop` shuts the libp2p node down and releases the libstorage context.
Like `start` it is asynchronous; the return confirms the stop command
was sent, and a `storageStop` event follows in the log. The node can be
started again later by calling `start @config.json`.

```bash
logoscore call storage_module stop
```

```bash
sleep 2
```

### 4.21 Stop the daemon

Shut the daemon down cleanly:

```bash
logoscore stop
```

The daemon removes its state file and exits.

```bash
sleep 2
```

### 4.24 Confirm the daemon has stopped

With no daemon running, the client reports `not_running` and exits
non-zero, so we add `|| true` to let the doc-test assert on the output:

```bash
logoscore status
```
