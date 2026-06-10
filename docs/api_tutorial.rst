Storage Module Tutorial
=======================

In this tutorial, you will build a simple CLI application that uploads and downloads files over the Logos Storage network using the :doc:`Logos Storage Module API <api_reference>`.

We provide a `Logos Storage App Skeleton <https://github.com/logos-storage/logos-storage-app-skeleton>`_ which provides a ready-made entry point with access to the ``LogosModules`` object required for accessing the API, as well as a set of simple synchronization utilities (a ``Waiter`` that blocks until an event fires) which make this tutorial simpler. The skeleton's own code uses only standard C++ types.

This module uses the **universal** interface: the implementation is plain C++ (``StorageModuleImpl``).

Prerequisites
-------------

- `Nix <https://nixos.org/download.html>`_ package manager
- Git

Building the Skeleton App
-------------------------

1. Clone the skeleton repository:

   .. code-block:: bash

      git clone https://github.com/logos-storage/logos-storage-app-skeleton.git
      cd logos-storage-app-skeleton

2. Build with Nix:

   .. code-block:: bash

      nix build

3. The compiled binary will be at ``./result/bin/storage-app``.

The skeleton provides an ``app_main`` entry point with access to the ``LogosModules`` object -- referred to as ``m_logos`` in the code snippets -- which contains the reference to the actual module object.

Return Types
------------

Most methods return ``StdLogosResult``, a struct from ``logos-cpp-sdk``:

.. code-block:: cpp

   struct StdLogosResult {
       bool success = false;
       nlohmann::json value;  // string, number, bool, object, array, or null
       std::string error;
   };

Always check ``success`` first, then read ``value`` or ``error``:

.. code-block:: cpp

   StdLogosResult result = m_logos->storage_module.peerId();
   if (!result.success) {
       // result.error contains the error message
   } else {
       std::string peerId = result.value.get<std::string>();
   }

Two methods return ``bool`` directly: ``init()`` (node creation) and ``start()`` (async command dispatch).

Events
------

Asynchronous operations report completion through named events. Subscribe with the typed ``on<EventName>`` accessors the wrapper generates for each declared event (e.g. ``onStorageStart``). The callback receives the event's JSON-encoded payload as a ``std::string``; parse it with ``nlohmann::json``. Common fields:

.. list-table::
   :header-rows: 1

   * - Field
     - Type
     - Description
   * - ``success``
     - bool
     - Whether the operation succeeded
   * - ``message``
     - string
     - Error description on failure
   * - ``sessionId``
     - string
     - Session identifier (uploads, downloads)
   * - ``cid``
     - string
     - Content identifier (on upload done)
   * - ``bytes``
     - number
     - Bytes transferred (progress events, file mode)
   * - ``chunk``
     - string
     - Base64-encoded data (chunk-mode download progress)

Initializing the Module
-----------------------

Before performing any operations, initialize the storage module with a JSON configuration string. See the :doc:`API Reference <api_reference>` for all available options.

.. code-block:: cpp

   const std::string jsonConfig = "{}";
   bool ok = m_logos->storage_module.init(jsonConfig);

.. caution::

   Do not call ``init()`` more than once per instance unless you call ``destroy()``. The result is a boolean in order to be compatible with Logos headless mode.

Starting the Node
-----------------

``start()`` dispatches the start command asynchronously. It returns ``true`` if the command was accepted; the actual outcome arrives via the ``storageStart`` event:

.. code-block:: cpp

   m_logos->storage_module.onStorageStart([this](const std::string& payload) {
       auto j = nlohmann::json::parse(payload);
       if (!j["success"].get<bool>()) {
           std::string error = j["message"].get<std::string>();
           // Handle error
       } else {
           // Node is ready
       }
   });

   bool ok = m_logos->storage_module.start();

Uploading a File
----------------

The simplest way to upload a file is with ``uploadUrl``, passing a local file path:

.. code-block:: cpp

   // Subscribe to events
   m_logos->storage_module.onStorageUploadDone([this](const std::string& payload) {
       auto j = nlohmann::json::parse(payload);
       std::string sessionId = j["sessionId"].get<std::string>();
       if (j["success"].get<bool>()) {
           std::string cid = j["cid"].get<std::string>();
           // Share this CID to let others download the content
       } else {
           std::string error = j["error"].get<std::string>();
           // Handle error
       }
   });

   m_logos->storage_module.onStorageUploadProgress([this](const std::string& payload) {
       auto j = nlohmann::json::parse(payload);
       int64_t bytes = j["bytes"].get<int64_t>();
       // Show upload progress
   });

   // Start the upload
   StdLogosResult result = m_logos->storage_module.uploadUrl("/path/to/myfile");
   if (!result.success) {
       // result.error describes the failure
   }
   std::string sessionId = result.value.get<std::string>();

The method is asynchronous: ``result.success`` only confirms that the upload was initiated. The completion event delivers a **CID** (Content Identifier), a string that uniquely identifies the file within the network. Progress events are throttled to at most one per percentage point.

You can pass an optional chunk size; the default is recommended for most cases:

.. code-block:: cpp

   int chunkSize = 1024 * 64;
   StdLogosResult result = m_logos->storage_module.uploadUrl("/path/to/myfile", chunkSize);

Streaming Upload
~~~~~~~~~~~~~~~~

For more control, use the streaming upload API. Use this only when ``uploadUrl`` cannot be used (e.g. you are streaming data that is not on disk):

.. code-block:: cpp

   // 1. Initialize the session
   StdLogosResult initResult = m_logos->storage_module.uploadInit(filename);
   if (!initResult.success) {
       // initResult.error describes the failure
       return;
   }
   std::string sessionId = initResult.value.get<std::string>();

   // 2. Upload chunks
   std::ifstream file(filepath, std::ios::binary);
   int chunkSize = 1024 * 64;
   std::string chunk(chunkSize, '\0');
   while (file.read(chunk.data(), chunkSize) || file.gcount() > 0) {
       chunk.resize(static_cast<size_t>(file.gcount()));
       StdLogosResult chunkResult = m_logos->storage_module.uploadChunk(sessionId, chunk);
       if (!chunkResult.success) {
           // Handle error
           break;
       }
       chunk.resize(chunkSize);
   }

   // 3. Finalize to get the CID
   StdLogosResult finalResult = m_logos->storage_module.uploadFinalize(sessionId);
   if (finalResult.success) {
       std::string cid = finalResult.value.get<std::string>();
   }

Downloading a File
------------------

To download content into a local file, you need its **CID**:

.. code-block:: cpp

   // Subscribe to events
   m_logos->storage_module.onStorageDownloadDone([this](const std::string& payload) {
       auto j = nlohmann::json::parse(payload);
       if (j["success"].get<bool>()) {
           // Download complete
       } else {
           std::string error = j["error"].get<std::string>();
           // Handle error
       }
   });

   m_logos->storage_module.onStorageDownloadProgress([this](const std::string& payload) {
       auto j = nlohmann::json::parse(payload);
       int64_t bytes = j["bytes"].get<int64_t>();
       // Show download progress
   });

   // Start the download
   StdLogosResult result = m_logos->storage_module.downloadToUrl(cid, "/path/to/output");
   if (!result.success) {
       // result.error describes the failure
   }
   std::string sessionId = result.value.get<std::string>();

.. note::

   Pass the ``local`` parameter (``downloadToUrl(cid, path, local)``) as ``true`` to only retrieve locally-cached data, or ``false`` to fetch from the network. If unsure, use ``false``.

Streaming Download
~~~~~~~~~~~~~~~~~~

If you want to process the data without writing it to disk, use ``downloadChunks``. In chunk mode, the ``storageDownloadProgress`` payload carries a ``chunk`` field (base64-encoded bytes) instead of ``bytes``:

.. code-block:: cpp

   m_logos->storage_module.onStorageDownloadProgress([this](const std::string& payload) {
       auto j = nlohmann::json::parse(payload);
       std::string b64chunk = j["chunk"].get<std::string>();
       // Decode from base64 before processing
   });

   StdLogosResult result = m_logos->storage_module.downloadChunks(cid);

For large files, prefer ``downloadToUrl`` which writes directly to a file without the base64 overhead.

Cleaning Up
-----------

Always stop the node before destroying resources:

.. code-block:: cpp

   m_logos->storage_module.onStorageStop([this](const std::string& payload) {
       auto j = nlohmann::json::parse(payload);
       bool success = j["success"].get<bool>();
   });

   StdLogosResult result = m_logos->storage_module.stop();
   // Wait for the storageStop event...
   result = m_logos->storage_module.destroy();

.. caution::

   It is STRONGLY recommended to stop the node before cleaning up resources. Not doing so can lead to undefined behavior (e.g. the node crashing) and data loss.

Headless Mode
-------------

The storage module can also be run from the command line without a UI:

.. code-block:: bash

   ./logos/bin/logoscore -m ./modules --load-modules storage_module \
     -c "storage_module.init(@config.json)" \
     -c "storage_module.start()" \
     -c "storage_module.importFiles(/path/to/files)"

Next Steps
----------

- See the :doc:`Storage Module API Reference <api_reference>` for the complete list of methods and events.
- If you need lower-level access or want to build bindings for another language, see the libstorage Tutorial.
