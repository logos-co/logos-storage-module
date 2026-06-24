# Logoscore

Logoscore can be run from any directory, not just the Storage Module root. Let's call this folder `logoscore-dir`.

This walkthrough uses two terminals:

- Terminal 1 runs the `logoscore` daemon and shows daemon/module logs.
- Terminal 2 runs client commands manually, or runs the optional helper script.

Keeping the daemon in a separate terminal makes the output much easier to read, especially while the storage node is emitting discovery, upload, and event logs.

## 1. Build logoscore

From `logoscore-dir`, build the `logoscore` CLI:

```bash
cd logoscore-dir
nix build 'github:logos-co/logos-logoscore-cli' --out-link ./logos
```

## 2. Create the modules directory

```bash
mkdir -p modules
```

#### 3. Build the package manager

```bash
nix --extra-experimental-features "nix-command flakes" build github:logos-co/logos-package-manager#cli --out-link ./package-manager
```

## 4. Build this module's LGX package

From the `logos-storage-module` checkout:

```bash
cd /path/to/logos-storage-module
nix build '.#lgx'
```

This creates an installable `.lgx` package under `result/`.

## 5. Install the LGX package

Return to `logoscore-dir` and install the package into `./modules`:

```bash
cd logoscore-dir
./package-manager/bin/lgpm --modules-dir ./modules install --dir /path/to/logos-storage-module/result/
```

For a local development build, `lgpm` may warn that the package is unsigned. That is expected.

## 6. Create `config.json`

Create a storage-node config in `logoscore-dir`:

```bash
cat > config.json <<'EOF'
{
  "data-dir": "./storage-data",
  "log-level": "DEBUG",
  "nat": "any",
  "network": "logos.test"
}
EOF
```

Notes:

- `data-dir` is where the storage node stores its local repo.
- `nat: none` is useful for a simple local run, but if the node only has private/local addresses, libstorage may warn that the node is only reachable on a private network. That warning does not prevent the local demo from working.

## 7. Start the daemon in Terminal 1

In Terminal 1, from `logoscore-dir`, start the daemon:

```bash
./logos/bin/logoscore -D -m ./modules
```

Leave this terminal open. It shows daemon and module logs.

Expected startup output includes lines like:

```text
Logoscore daemon started
Daemon state: .../.logoscore/daemon/state.json
Local client config: .../.logoscore/client/config.json
```

You may also see warnings like:

```text
Module capability_module carries no usable logos_protocol_version (pre-protocol build) - loading permissively
```

For this development walkthrough, that warning is non-fatal.

## 8. Drive the module from Terminal 2

In Terminal 2, wait until the daemon accepts commands, then load and call the module:

```bash
until ./logos/bin/logoscore status >/dev/null 2>&1; do sleep 0.2; done
./logos/bin/logoscore status

./logos/bin/logoscore load-module storage_module
./logos/bin/logoscore call storage_module init @config.json
./logos/bin/logoscore call storage_module start
```

The `init` and `start` calls should both return `true`. Watch Terminal 1 for storage-node startup logs and events.

Create a small import directory and upload files:

```bash
mkdir -p files
dd if=/dev/urandom of=./files/data1k.bin bs=1024 count=1 status=none
dd if=/dev/urandom of=./files/data1M.bin bs=1M count=1 status=none
dd if=/dev/urandom of=./files/data10M.bin bs=10M count=1 status=none

./logos/bin/logoscore call storage_module importFiles "$PWD/files"
```

Expected daemon-side signs of success include `storageUploadProgress` and `storageUploadDone` events.

After uploads complete, list local manifests:

```bash
./logos/bin/logoscore call storage_module manifests
```

Stop the daemon when done:

```bash
./logos/bin/logoscore stop
```

You can remove the generated sample files afterward:

```bash
rm -rf ./files
```

## 9. Optional helper script

The manual commands above are intentionally compact. For repeated local testing, this repository also provides a convenience script:

```text
docs/logoscore-overview/run-logoscore-storage-demo.sh
```

The script performs the Terminal 2 workflow for you. It waits for the daemon, checks status, verifies that `storage_module` is not already loaded, optionally removes the configured `data-dir` before initialization, loads and starts the module, creates sample files, imports them, lists manifests, stops the daemon, and removes the generated sample files.

To use it, start the daemon in Terminal 1 as shown above. Then run this in Terminal 2:

```bash
cd logoscore-dir
cp /path/to/logos-storage-module/docs/logoscore-overview/run-logoscore-storage-demo.sh .
./run-logoscore-storage-demo.sh
```

Expected successful signs:

- `logoscore status` reports that the daemon is running.
- `load-module storage_module` succeeds.
- `init @config.json` returns `true`.
- `start` returns `true`.
- The daemon terminal shows storage node startup logs.
- The daemon terminal shows upload progress and `storageUploadDone` events after `importFiles`.
- The `manifests` call returns entries for the imported files.

With the example `nat: none` config, the daemon may print warnings such as:

```text
Bind IP is not a public IP address. Should not use --nat:none option
Unable to determine a public IP address. This node will only be reachable on a private network.
```

Those warnings are expected for a simple local/private-network run and do not prevent the demo from completing.

The helper script stops the daemon at the end with:

```bash
./logos/bin/logoscore stop
```
