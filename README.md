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
./logos/bin/logoscore -m ./modules --load-modules storage_module -c "storage_module.init(@config.json)" -c "storage_module.start()" -c "storage_module.importFiles(/path/to/import/files)"
```

This command does three things: initialize the module from `config.json`, start the node, and import files from the specified directory.

You should see logs similar to:

```
Debug: [LOGOS_HOST "storage_module" ]: "LogosAPIClient: Received event: \"storageUploadDone\""
Debug: [LOGOS_HOST "storage_module" ]: "LogosAPIClient: Emitting event: \"storageUploadDone\""
Debug: [LOGOS_HOST "storage_module" ]: "File \"CMakeLists.txt\" uploaded successfully, session: \"0\" cid= \"zDvZRwzkyHVgr59zFkX7vyfzK7oUP7Jc6k7qpFD9ssDi7V5fvdjw\""
Debug: [LOGOS_HOST "storage_module" ]: "importFiles completed: 1 / 1 files uploaded"
```

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

Then you need to build libstorage:

```bash
cd vendor/logos-storage-nim
make libstorage
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
logos-storage-nim
```

Ensure that libstorage is built in logos-storage-nim folder.

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

Qt Creator provides a great development experience for Qt. To ensure proper integration, it is recommended to either configure the project using submodules or clone the dependencies independently into the same parent directory. Nix *may* work with Qt Creator, but only after an initial build has been run.

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

Enable CMake debug logging, add `--log-level=DEBUG` in `Projects` -> `Imported Kits` -> `Build` -> `Additional CMake options`.

Ensure that `clangd` is enabled for your project. Go to `Projects` on the left, then click on `Manage Kits` at the top. Select the `C++` tab and open the last tab, `Clangd`. Check `Use clangd` and, if needed, configure it to use the `clangd` installed on your system.

That’s it. The configuration defined in `CMakeLists.txt` should allow the project to build correctly.

If you encounter any configuration issues, close Qt Creator, remove the `CMakeLists.txt.user` file, and restart Qt Creator to reconfigure the project.

### API

This tutorial will explain how to use the API for basic operations, i.e., upload and download operations.

This tutorial assumes that you already have the [sdk](https://github.com/logos-co/logos-cpp-sdk) and [logos core](https://github.com/logos-co/logos-liblogos). Please refer to the respective documentations to setup your project. 

`m_logos` refers to a `LogosModules` instance that is supposed to be already created. 

The API has been designed to work with SDK architecture and Qt Remote Objects. Technical choices have been made with the constraints of this architecture. We assume familiarity with both.

`Logos Storage` refers to the [nim project](https://github.com/logos-storage/logos-storage-nim) hosting the actual code of the Storage engine.

`Logos Storage Module` refers to this project.

#### Logos Result

The API calls return a `LogosResult` object which contains 2 fields: `success` (boolean) and `value` (QVariant). By looking into the `success` variable, you will be able to extract the error or the actual value. The expected type has to be passed in the `getValue` accessor. The error is expected to be a string. Example:

```cpp
LogosResult result = m_logos->storage_module.someOperation(jsonConfig);

if(!result.success) {
    QString error = result.getValue<QString>();
} else {
    int actualValue = result.getValue<int>();
}
```

You can refer to the [SDK documentation](https://github.com/logos-co/logos-cpp-sdk/blob/c3477d29e32cae5f73ca637fb81e547f8a6cba58/README.md?plain=1#L182) for an exhaustive documentation of `LogosResult`.

#### Init

Before using the Logos Storage Module you need to initialize it by calling the function `init`:

```cpp
const QString jsonConfig = "{}";
bool result = m_logos->storage_module.init(jsonConfig);
```

Note that this method returns a boolean and not a `LogosResult` because of the headless mode compability.

You can check the possible values of the JSON configuration in the header definition.

**Important note**:

You should not call `init` more than once per instance of the Logos Storage Module. Running several instances of Logos Storage requires several instances of the Storage Module and is out of the scope of this tutorial.

#### Lifecycle

Before interacting with the Logos Storage Module, you need to start the Logos Storage node. To do this, you need to run:

```cpp
bool result = m_logos->storage_module.start();
```

The result object returns a success if the command was successfully sent to the node, but it does not mean that the command itself was successful! To know that, you need to listen to the `storageStart` event:

```cpp
m_logos->storage_module.on("storageStart", [this](const QVariantList& data) {
    bool success = data[0].toBool();

    if (!success) {
        QString message = data[1].toString();
        // Handle Error
    } else {
        // success
    }
})
```

The data received is a `QVariantList` containing 2 items: the boolean success and a message. If the first element is false i.e., we have a failure, then the second element contains the error message. If it is true; i.e, the operation ran successfully, the message will contain an empty string. 

Similarly, the `stop` function works the same way with the `storageStop` event:


```cpp
m_logos->storage_module.on("storageStop", [this](const QVariantList& data) {
    bool success = data[0].toBool();

    if (!success) {
        QString message = data[1].toString();
        // Handle Error
    } else {
        // success
    }
})
LogosResult result = m_logos->storage_module.stop();
```

The Logos Storage Module will not stop or clean up the node automatically, and it is the application's responsibility to do so at the appropriate time (e.g. before quitting). Not shutting down the node properly can lead to data loss.

**Important Note**: It is STRONGLY recommended to stop the node before cleaning up the resources. Not doing so can lead to undefined behavior (e.g. node crashing).

To cleanup the resources, you can just call the synchronous `destroy` function:

```cpp
LogosResult result = m_logos->storage_module.destroy();
```

#### Upload a file

##### Recommended

The straightforward way to upload a file is to use the `uploadUrl` function. It works well with `FileDialog` and QML:

```cpp
void upload(const QUrl& url) {
    LogosResult result = m_logos->storage_module.uploadUrl(url);
}
```

You can pass an extra parameter which is the chunk size of the uploaded data. It is recommended that you keep the default value unless you have a reason to do otherwise:

```cpp
void upload(const QUrl& url) {
    int chunkSize = 1024 * 64;
    LogosResult result = m_logos->storage_module.uploadUrl(url, chunkSize);
}
```

The method is asynchronous, so again, result tells you that the command was sent to Logos Storage but it doesn't tell you that it was successful. You need to add a listener for `storageUploadDone`:

```cpp
m_logos->storage_module.on("storageUploadDone", [this](const QVariantList& data) {
    bool success = data[0].toBool();
    QString sessionId = data[1].toString();

    if (!success) {
        QString message = data[2].toString();
        // Handle error
    } else {
        m_cid = data[2].toString();
        // Do something super cool with the CID
    }
})
```

The callback gives 3 values:

1. success: a bool that is true if it was successful
2. sessionId: the sessionId of your upload
3. message: The error message on failure, the CID on success

The CID is an identifier of your content. Share it out-of-band to let other people download your content (see [download a file](#download-a-file)).

That's cool but you may need to track the progress of your upload, maybe to display a nice progress bar. You can do it by adding a listener for `storageUploadProgress`.

```cpp
m_logos->storage_module.on("storageUploadProgress", [this](const QVariantList& data) {
    bool success = data[0].toBool();
    QString sessionId = data[1].toString();

    if (!success) {
        QString message = data[2].toString();
        // Handle error
    } else {
        int bytes = data[2].toInt();
    }
})
```

Be careful! Depending on the size of your data and the chunk size, this function could be called A LOT of times. If you are doing some QML rendering here it is going to block your UI. The advice is to update your UI (your progress bar for example) using a threshold value.

##### Advanced

There is an advanced API that you should use only if you cannot use the `uploadUrl`. This API allows you to upload content using a stream. Because of the constraints of Qt Remote Objects, the API cannot just accept a stream / pointer, so the API is a bit more complex but provides more control over the upload. Let's go over the steps.

First you need to create an upload session by providing the filename:

```cpp
    QString filename = "...";
    LogosResult result = m_logos->storage_module.uploadInit(filename);

    // Or
    QString filename = "...";
    int chunkSize = 1024 * 64;
    LogosResult result = m_logos->storage_module.uploadInit(filename, chunkSize);

    // Extract the session Id
    if(result.success) {
        QString sessionId = result.getValue<QString>();
    }

```

The filename is used to identify the metadata of your content and is useful information in the Manifest. A manifest is an object containing the information about the data identified by a CID. For more information, the dataset spec is available [here](https://lip.logos.co/storage/raw/datasets.html).

The result of `uploadInit` will provide you the `sessionId`. You can use this identifier to upload your chunks one by one:

```cpp
// It is supposed that you have a valid QFile.
QFile file;

while (!file.atEnd()) {
    QByteArray chunk = file.read(chunkSize);
    bytesRead += chunk.size();

    result = m_logos->storage_module.uploadChunk(sessionId, chunk);

    if (!result.success) {
        // Handle error here
    }
}
```

You can subscribe to the same progress event `storageUploadProgress` to get the progress.

While this requires more code, it provides more control. You can easily stop uploading the chunks and resume the upload at any time!

After all the chunks are sent, you need to finalize the upload to get the CID:

```cpp
    LogosResult result = m_logos->storage_module.uploadFinalize(sessionId);

    if(result.success) {
        QString cid = result.getValue<QString>();
    }

```

Then you can share this CID with others to let them be able to download the file.

#### Download a file

##### Save into a file

The easiest way to download a file is to use the `downloadToUrl` method:

```cpp
    QString cid = "...";
    QUrl url = ...;

    LogosResult result = m_logos->storage_module.downloadToUrl(cid, url);

    // Or
    bool local = false;
    LogosResult result = m_logos->storage_module.downloadToUrl(cid, url, local);

    // Or
    int chunkSize = 1024 * 64;
    LogosResult result = m_logos->storage_module.downloadToUrl(cid, url, local, chunkSize);
```

If `local` is set to true, this returns data that is already local to the node; i.e., if your node already has the file, then `downloadToUrl` will read that file and copy it into the specified URL, otherwise it will fail. If `local` is set to false, instead, the node might go through the network to fetch data from other nodes if it is not locally available. If you are unsure, just set this to false.

To get the download progress, subscribe to `storageDownloadProgress`. Note that you will get progress events even for locally available data.

The callback gives 3 values:

1. success: a bool that is true if it was successful
2. sessionId: the sessionId of your download
3. size: The number of bytes downloaded


```cpp
m_logos->storage_module.on("storageDownloadProgress", [this](const QVariantList& data) {
    bool success = data[0].toBool();
    QString sessionId = data[1].toString();

    if (!success) {
        QString message = data[2].toString();
        // Handle error
    } else {
        int size = data[2].toInt();
        // Show a progress download
    }
})
```

To get the completion event, subscribe to `storageDownloadDone`.

The callback gives 3 values:

1. success: a bool that is true if it was successful
2. sessionId: the sessionId of your download
3. message: An empty string on success

```cpp
m_logos->storage_module.on("storageDownloadDone", [this](const QVariantList& data) {
    bool success = data[0].toBool();

    if (!success) {
        QString message = data[1].toString();
        // Handle error
    } else {
        // success
    }
})
```

##### Handle the chunks manually

If you do not want to save the data to a file but you want to stream it somewhere you can use `downloadChunks`:

```cpp
QString cid = "...";

LogosResult result = m_logos->storage_module.downloadChunks(cid);

// Or
bool local = false;
LogosResult result = m_logos->storage_module.downloadChunks(cid, local);

// Or
int chunkSize = 1024 * 64;
LogosResult result = m_logos->storage_module.downloadChunks(cid, local, chunkSize);
```

You can subscribe to the same events: `storageDownloadProgress` and `storageDownloadDone`. But there is an important difference. For `storageDownloadProgress`, the callback values are:

1. success: a bool that is true if it was successful
2. sessionId: the sessionId of your download
3. chunk: The chunk of data downloaded

**Important note**: Because of Qt Remote Objects constraints, the chunk is COPIED when passed to the callback. If performance is crucial (working with big files) prefer the `downloadToUrl` method.

#### Data management

Several methods are available to manage the data in your node.

- `exists`: Verify that a manifest exists for the CID in your local store.
- `fetch`: Download a file in the background to your store (you won't receive any event).
- `remove`: Remove a file from your local storage.
- `space`: Gets the maximum amount of disk space your node is allowed to occupy.
- `manifests`: Get the manifests of the files in your local node.
- `downloadManifest`: Download a manifest to get information about a CID.

#### Debug / info

Several methods are available for debugging your Logos Storage:

- `dataDir`: Get the folder that contains the Logos Storage data.
- `debug`: Get debug information such as peers, SPR, etc.
- `spr`: Get the SPR of your node.
- `peerId`: Get the peer ID of your node.
- `updateLogLevel`: Change the log level of Logos Storage node.

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
