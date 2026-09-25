# Preparing and Running a Logos Node with Storage

In this tutorial, you will learn how to prepare a Logos storage node from source.
This involves:

1. Building the Logos runtime (`logoscore`) and the local package manager (`lgpm`).
2. Building **this** storage module as an installable package.
3. Installing the package with `lgpm`.
4. Starting the Logos daemon, and then using the CLI to:
  - load `storage_module`;
  - introspect it with `module-info`;
  - drive a real node lifecycle: initialise it from a config, start the storage node,
    read its identity, upload a local file, download it back, and stop it again —
    verifying the module actually runs and round-trips real values through libstorage.

Note that this is an **executable tutorial**: it is run automatically on every
module change. Having a successful run (shown in the header) means that commit/version
that this document has been built again should load and run correctly.

**What you'll build:** This `storage_module`, packaged as `.lgx`, installed with `lgpm`, and driven through the `logoscore` daemon — node start, a local file upload, a download round-trip, and shutdown.

**What you'll learn:**

- How to build the Logos runtime and the `lgpm` package manager from source
- How a module's build system (Nix flake) exposes a ready-to-install `.lgx` via its `#lgx` output
- How to install an `.lgx` into a modules directory with `lgpm`
- How to start the `logoscore` daemon, load a module, introspect it, and call its methods
- How to initialise, start, exercise, and stop a libstorage node headlessly
- How to upload and download a file through the module's `uploadUrl` and `downloadToUrl` methods
- How to shut the daemon down and confirm it has exited

## Prerequisites

- **Nix** with flakes enabled. Install from [nixos.org](https://nixos.org/download.html), then enable flakes:

```bash
mkdir -p ~/.config/nix
echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
```

Verify: `nix flake --help >/dev/null 2>&1 && echo "Flakes enabled"`

- **A Linux or macOS machine.**
- **`jq`** on your `PATH` — used to pull the uploaded CID out of the `manifests` JSON. Verify: `jq --version`

---

## Step 1: Build the Logos daemon

Build the Logos runtime CLI from its published flake. The result outputs a binary
named `logoscore` under a symlinked directory named `./logos`. `logoscore` is the headless
frontend for [`logos-liblogos`](https://github.com/logos-co/logos-liblogos) which runs as
a daemon - it brings in the whole module-runtime stack we need.

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

### 3.1 Build the module's .lgx

Build the `#lgx` output and link it as `./storage-lgx`. (This compiles
the module and its libstorage dependency through Nix, so the first build
is slow.)

```bash
# From inside the clone this is simply: nix build '.#lgx'
nix build 'github:logos-co/logos-storage-module#lgx' -o storage-lgx
```

The `.lgx` package is now under `./storage-lgx/`:

```bash
ls storage-lgx/*.lgx
```

### 3.2 Install the .lgx with lgpm

Install the freshly-built package into `./modules`. `storage_module` is
a `core` module, so it goes to `--modules-dir`. The package is unsigned
(a local dev build), so we pass `--allow-unsigned`.

```bash
./lgpm/bin/lgpm --modules-dir ./modules --allow-unsigned install --file storage-lgx/*.lgx
```

### 3.3 Confirm the install

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

`storage_module.init` takes a JSON configuration string. We keep it
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
    "log-file": "$(pwd)/storage-data/storage.log",
    "nat": "extip:127.0.0.1"
}
EOF
```

### 4.9 Initialise the node

`init` creates and configures a libstorage node from the JSON config.
The `@config.json` syntax loads the file's contents as the argument. It
is synchronous and returns `true` on success:

```bash
logoscore call storage_module init @config.json
```

### 4.10 Start the node

`start` brings the libp2p node online. It is asynchronous: the return
value only confirms the start command was accepted; the real outcome is
delivered as a `storageStart` event in the daemon log. The remaining
calls all run against this started node.

```bash
logoscore call storage_module start
```

### 4.11 Wait for the node to come up

Starting a libp2p node takes a moment. Give it a few seconds before
querying the node, then inspect the log for the `storageStart` event:

```bash
sleep 5
```

```bash
cat logs.txt
```

The emitted `storageStart` event carries `{ "success": true, ... }`.

### 4.12 Inspect the node with debug

`debug` returns a JSON object describing the running node. It contains
a lot of useful information like the peerId, spr ...etc.
We assert with `jq` that the node's `id` and `spr` came back non-empty:

```bash
logoscore call storage_module debug
```

### 4.13 List manifests (empty baseline)

`manifests` lists everything stored locally. On a fresh node this is an
empty array — we'll call it again after an upload to see it change:

```bash
logoscore call storage_module manifests
```

### 4.14 Create the file to upload

Create a small file in the working directory. We upload it in the next
step:

```
Hello from the logos-storage-module doc-test.
```

### 4.15 Upload a local file

Upload the file with `uploadUrl`. It takes an **absolute** path (the
daemon resolves it from its own working directory), a chunk size in
bytes, and an `advertise` flag (`true` here). It returns a session ID;
the upload itself runs in the background. On a fresh `fs` node with
no peers the blocks are stored locally, so this is a real, fully-offline
round-trip. We assert on `"success":true` so a rejected upload fails
here rather than silently later:

```bash
logoscore call storage_module uploadUrl "$(pwd)/hello.txt" 65536 true
```

### 4.16 Wait for the upload to complete

The upload runs in the background, so give it a few seconds before
checking that the file landed, then inspect the log for the
`storageUploadDone` event and its CID:

```bash
sleep 1
```

```bash
cat logs.txt
```

The emitted `storageUploadDone` event carries the new content's `cid` —
proof the file was chunked, stored, and a manifest written.

### 4.17 List manifests (now populated)

Call `manifests` again. The uploaded file now appears as a stored
manifest — we assert on presence rather than the (non-deterministic) CID:

```bash
logoscore call storage_module manifests
```

### 4.18 Capture the uploaded CID

`downloadToUrl` needs the content's CID. We pull it out of the first
`manifests` entry with `jq` and save it to `cid.txt`. Each command runs in
its own shell, so we pass the value to the next step through a file rather
than a shell variable:

```bash
logoscore call storage_module manifests \
  | jq -er '.result.value[0].cid' > cid.txt
```

### 4.19 Download the file back

`downloadToUrl` fetches the content for a CID and writes it to a local
file. It takes the CID, an **absolute** destination path, a `local` flag,
a chunk size in bytes, `isPrivate`, and `advertise`. We use
`isPrivate=false` and `advertise=true`. We pass `local` as `true`: the upload stored
the blocks in this node's own repo, so the download reads them straight
back with no network. Like `uploadUrl` it is asynchronous and returns a
session ID immediately; completion arrives as a `storageDownloadDone`
event in the log.

```bash
logoscore call storage_module downloadToUrl "$(cat cid.txt)" "$(pwd)/downloaded.txt" true 65536 false true
```

### 4.20 Wait for the download to complete

The download runs in the background, so give it a few seconds, then
inspect the log for the `storageDownloadDone` event:

```bash
sleep 1
```

```bash
cat logs.txt
```

The emitted `storageDownloadDone` event carries `{ "success": true, ... }`.

### 4.21 Verify the round-trip

Read the downloaded file back. Its contents are identical to the file we
uploaded — proof the upload/download round-trip preserved the data
exactly:

```bash
cat downloaded.txt
```

### 4.22 Check the content exists locally

`exists` reports whether the content for a CID is in local storage. After
the upload it returns `true`:

```bash
logoscore call storage_module exists "$(cat cid.txt)"
```

### 4.23 Watch for the manifest event

Because `downloadManifest` is asynchronous, we need to watch for the
`storageDownloadManifestDone` event before triggering the fetch.

```bash
logoscore watch storage_module --event storageDownloadManifestDone --json
```

```bash
sleep 2
```

### 4.24 Fetch the manifest

Call `downloadManifest`. It returns immediately; the real result is
delivered to the watcher started above:

```bash
logoscore call storage_module downloadManifest "$(cat cid.txt)" false true
```

### 4.25 Confirm the manifest event arrived

Give the event a moment to land, then inspect what the watcher
captured. The `storageDownloadManifestDone` event carries
`{ "success": true, "cid": ..., "manifest": { ... } }` — the metadata
describing how the content is stored:

```bash
cat manifest-event.txt
```

### 4.26 Remove the content

`remove` deletes the content for a CID from local storage. The delete
may take a while, so it runs in the background: the call returns immediately
and the outcome arrives as a `storageRemoveDone` event in the log.

```bash
logoscore call storage_module remove "$(cat cid.txt)"
```

### 4.27 Wait for the removal to complete

The removal runs in the background, so give it a moment, then inspect
the log for the `storageRemoveDone` event:

```bash
sleep 1
```

### 4.28 Confirm the content is gone

Call `exists` again; with the content removed it now returns `false`:

```bash
logoscore call storage_module exists "$(cat cid.txt)"
```

### 4.29 Stop the node

`stop` shuts the libp2p node down. Like `start` it is asynchronous; the
return confirms the stop command was sent, and a `storageStop` event
follows in the log. The node can be started and stopped multiple times.

```bash
logoscore call storage_module stop
```

```bash
sleep 2
```

### 4.30 Destroy the node

`destroy` closes and frees the storage context. It is synchronous and
must be called after the node is stopped:

```bash
logoscore call storage_module destroy
```

### 4.31 Stop the daemon

Shut the daemon down cleanly:

```bash
logoscore stop
```

The daemon removes its state file and exits.

```bash
sleep 2
```

### 4.32 Confirm the daemon has stopped

With no daemon running, the client reports `not_running` and exits
non-zero, so we add `|| true` to let the doc-test assert on the output:

```bash
logoscore status
```
