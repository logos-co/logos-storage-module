Storage Module Tutorial
=======================

In this tutorial, you will build a simple CLI application that uploads and downloads files over the Logos Storage network using the :doc:`Logos Storage Module API <api_reference>`.

We provide a `Logos Storage App Skeleton <https://github.com/logos-storage/logos-storage-app-skeleton>`_ which provides a ready-made entry point with access to the ``LogosModules`` object required for accessing the API, as well as a set of simple Qt-compatible synchronization utilities which make this tutorial simpler.

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

The skeleton provides an ``app_main`` entry point with access to the ``LogosModules`` object -- referred to as `m_logos` in the code snippets -- which contains the reference to the actual module object. It also provides Qt-compatible synchronization utilities for blocking on asynchronous operations.

Initializing the Module
-----------------------

Before performing any operations, initialize the storage module with a JSON configuration string. See the :doc:`API Reference <api_reference>` for all available options.

.. code-block:: cpp

   const QString jsonConfig = "{}";
   bool result = m_logos->storage_module.init(jsonConfig);

.. caution::

   Do not call ``init()`` more than once per instance unless you call ``destroy()``. Note that the result is a boolean in order to be compatible with Logos headless mode.

Starting the Node
-----------------

Start the node and listen for the completion event:

.. code-block:: cpp

   m_logos->storage_module.on("storageStart", [this](const QVariantList& data) {
       bool success = data[0].toBool();
       if (!success) {
           QString error = data[1].toString();
           // Handle error
       }
   });

   bool result = m_logos->storage_module.start();

Uploading a File
----------------

The simplest way to upload a file is with ``uploadUrl``:

.. code-block:: cpp

   // Subscribe to events
   m_logos->storage_module.on("storageUploadDone", [this](const QVariantList& data) {
       bool success = data[0].toBool();
       QString sessionId = data[1].toString();
       QString cidOrError = data[2].toString();

       if (success) {
           qDebug() << "Upload complete. CID:" << cidOrError;
       } else {
           qDebug() << "Upload failed:" << cidOrError;
       }
   });

   m_logos->storage_module.on("storageUploadProgress", [this](const QVariantList& data) {
       bool success = data[0].toBool();
       QString sessionId = data[1].toString();
       int bytes = data[2].toInt();
       qDebug() << "Uploaded" << bytes << "bytes";
   });

   // Start the upload
   QUrl fileUrl = QUrl::fromLocalFile("/path/to/myfile");
   LogosResult result = m_logos->storage_module.uploadUrl(fileUrl);

The upload returns a **CID** (Content Identifier) — a string that uniquely identifies the file within the network.

Streaming Upload
~~~~~~~~~~~~~~~~

For more control, use the streaming upload API:

.. code-block:: cpp

   // 1. Initialize the session
   LogosResult result = m_logos->storage_module.uploadInit(filename);
   QString sessionId = result.getValue<QString>();

   // 2. Upload chunks
   QFile file(filepath);
   file.open(QIODevice::ReadOnly);
   int chunkSize = 1024 * 64;
   while (!file.atEnd()) {
       QByteArray chunk = file.read(chunkSize);
       result = m_logos->storage_module.uploadChunk(sessionId, chunk);
       if (!result.success) {
           // Handle error
           break;
       }
   }

   // 3. Finalize
   result = m_logos->storage_module.uploadFinalize(sessionId);
   if (result.success) {
       QString cid = result.getValue<QString>();
       qDebug() << "CID:" << cid;
   }

Downloading a File
------------------

To download content, you need the **CID** and the **Signed Peer Record** (SPR) of a node that has the content:

.. code-block:: cpp

   // Subscribe to events
   m_logos->storage_module.on("storageDownloadDone", [this](const QVariantList& data) {
       bool success = data[0].toBool();
       QString message = data[1].toString();
       if (success) {
           qDebug() << "Download complete";
       } else {
           qDebug() << "Download failed:" << message;
       }
   });

   m_logos->storage_module.on("storageDownloadProgress", [this](const QVariantList& data) {
       bool success = data[0].toBool();
       QString sessionId = data[1].toString();
       int size = data[2].toInt();
       qDebug() << "Downloaded" << size << "bytes";
   });

   // Start the download
   QUrl destination = QUrl::fromLocalFile("/path/to/output");
   LogosResult result = m_logos->storage_module.downloadToUrl(cid, destination);

.. note::

   Set the ``local`` parameter to ``true`` to only retrieve locally-cached data. Set it to ``false`` (default) to fetch from the network.

Cleaning Up
-----------

Always stop the node before destroying resources:

.. code-block:: cpp

   LogosResult result = m_logos->storage_module.stop();
   // Wait for storageStop event...
   result = m_logos->storage_module.destroy();

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

- See the :doc:`Storage Module API Reference <api_reference>` for the complete list of methods and events / signals.
- If you need lower-level access or want to build bindings for another language, see the libstorage Tutorial.
