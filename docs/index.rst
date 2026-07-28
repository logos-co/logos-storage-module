Logos Storage Module
====================

.. versionchanged:: 2.0.0
   ``downloadManifest`` is now asynchronous. It no longer returns the manifest
   directly: the call only reports whether the request was dispatched, and the
   manifest arrives later through the ``storageDownloadManifestDone`` event.
   Listen for that event instead of reading the return value:

   .. code-block:: json

      {
        "success": true,
        "cid": "zDvZRwzkAvNyTpfDp4Qns5wnDvN8KAGwa9eqUC9PvWWQ7C5wXkVD",
        "manifest": {
          "manifestVersion": 1,
          "treeCid": "zDzSvJTezk3JZjGW1aFvqsr9rD8AhQzbpsZApVZQ3hVKsZ9FaWp9",
          "datasetSize": 1048576,
          "blockSize": 65536,
          "filename": "photo.jpg",
          "mimetype": "image/jpeg"
        }
      }

   On failure the payload is
   ``{ "success": false, "cid": "...", "error": "..." }`` (no ``manifest``).

.. versionchanged:: 2.0.0
   ``remove`` is now asynchronous. The call only reports whether the request
   was dispatched; the real outcome arrives through the ``storageRemoveDone``
   event:

   .. code-block:: json

      {
        "success": true,
        "cid": "zDvZRwzkAvNyTpfDp4Qns5wnDvN8KAGwa9eqUC9PvWWQ7C5wXkVD"
      }

   On failure the payload is
   ``{ "success": false, "cid": "...", "error": "..." }``.

The Logos Storage Module lets your application share files over a
peer-to-peer (p2p) network.

Overview
--------

In a nutshell, to share a file on the Logos Storage network, you need to:

- **Run a Logos node:**. This involves downloading/compiling the Logos tooling,
  installing and loading the storage module, and configuring and starting your
  own node.
- **Publish a file** to your node, and get back a unique identifier assigned by Logos
  storage to it. This identifier is called a *Content IDentifier* (CID) and, in
  practical terms, it is just a short textual string.
- **Share the CID with anyone.** With the CID, any node on the same network can then
  download your file. Downloading *replicates* a file, so your file stays available
  even after you go offline, as long as one replica remains online.

The key portions of the :doc:`module API<api_reference>` involved in a publishing/downloading flow are:

1. ``init`` -- initialize the node and read its JSON configuration file.
2. ``start`` -- start the node and join the network.
3. ``uploadUrl`` / ``downloadToUrl`` -- send and receive files.
4. ``stop`` then ``destroy`` -- shut down cleanly.

See the `Tutorial
<https://logos-co.github.io/logos-doctest-hub/#logos-storage-module/ubuntu-latest/running-this-storage-module-against-logoscore>`_
for a full example, and the :doc:`API Reference <api_reference>` for every
method.

Configuration
-------------

You configure a node by passing a JSON string to ``init``. Every key is
optional: any key you leave out keeps its default value.

The options below are the ones you are most likely to need. For the full
list with default values, see the ``init`` method in the
:doc:`API Reference <api_reference>`.

.. list-table::
   :header-rows: 1
   :widths: 22 18 60

   * - Option
     - Default
     - Description
   * - ``log-level``
     - ``info``
     - How much detail the node writes to the log. From least to most
       detail: ``FATAL``, ``ERROR``, ``WARN``, ``NOTICE``, ``INFO``,
       ``DEBUG``, ``TRACE``. Use ``DEBUG`` when you need to troubleshoot.
   * - ``data-dir``
     - ``.cache/storage``
     - Folder where the node keeps its data and configuration. Use a stable
       path if you want your data to survive restarts.
   * - ``storage-quota``
     - ``21474836480`` (20 GiB)
     - Maximum disk space, in bytes, the node may use for stored content.
   * - ``listen-port``
     - ``0`` (random)
     - TCP port other peers use to connect to you. See `Connectivity`_.
   * - ``disc-port``
     - ``8090``
     - UDP port used to discover other peers. See `Connectivity`_.
   * - ``nat``
     - ``auto``
     - How the node finds its public address so others can reach it. See
       `Connectivity`_.
   * - ``network``
     - ``logos.test``
     - Which network the node joins. ``bootstrap-node`` (empty by default)
       overrides it. See `Connectivity`_.
   * - ``mix-enabled``
     - ``false``
     - Use the Mix privacy network. See `Mix`_.

Example:

.. code-block:: json

   {
     "log-level": "info",
     "data-dir": ".cache/storage",
     "storage-quota": 21474836480,
     "listen-port": 0,
     "disc-port": 8090,
     "nat": "auto",
     "network": "logos.test",
     "mix-enabled": false
   }

Connectivity
------------

A node is useful only when it can reach other nodes. This section explains
how a node joins a network and how to make it reachable from the outside.

Joining a network
~~~~~~~~~~~~~~~~~~

You can share files once you are part of a network formed by entry points
called bootstrap nodes. Once connected, the node discovers other
peers on its own. You have two choices.

**Join an existing network.** The easiest way is to set the ``network``
option to a preset name. The preset already contains that network's
bootstrap nodes, so you need nothing else.

.. list-table::
   :header-rows: 1

   * - Preset
     - Description
   * - ``logos.test``
     - Logos testnet (default)
   * - ``logos.dev``
     - Logos devnet
   * - ``codex.dev``
     - Codex legacy devnet (deprecated)

**Create your own network.** Start the first node with ``no-bootstrap-node``
set to ``true``: it bootstraps from no one and becomes the entry point. Read
its address with the ``spr`` method, then use that address as the
``bootstrap-node`` of every other node you want in the network.

Being reachable: NAT
~~~~~~~~~~~~~~~~~~~~~

On a home network, your node usually sits behind a router (NAT), so it is
not reachable from the internet by default.

With NAT traversal, the node will try different ways to become reachable.
After checking the reachability of its listen port, the node will try
the following actions in order if it is unreachable:

1. Try to open the ports: if the router has UPnP, NAT-PMP or PCP enabled,
   the node asks it to open the listen port for incoming connections.
   If that works, the node becomes reachable.
2. Go through a relay: if it fails, the node will use another peer as a
   relay. When a peer tries to connect to this node, it will be redirected
   to the relay, which will forward the connection to the node.
3. Escape the relay: ideally, when a peer arrives through the relay, the node
   tries to open a direct connection with it anyway (hole punching). If it
   works, the relay is dropped and the two nodes talk directly.

Being unreachable is no longer a dead end, the node behind relays will still
be able to share content but the performance will be lower than a reachable
node.

Note that the node keeps checking its reachability, so if it becomes
unreachable at some point, it will try again to become reachable.

The ``nat`` option controls how the node tries to become reachable from the
internet:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Value
     - When to use it
   * - ``auto``
     - Default. Everything described above.
   * - ``extip:<IP>``
     - Set your public IP yourself, e.g. ``extip:203.0.113.7``. The node
       announces that address as-is and skips the checks above. Use this when
       you know your public IP and have opened your listen port on the router
       yourself, or on a machine with a public IP (a cloud server or VPS).

.. note::

   Some Linux distributions (such as Fedora) enable a firewall by default
   that can block incoming connections even when your port mapping is
   correct. You may need to allow the port through the firewall.

Tuning NAT traversal
^^^^^^^^^^^^^^^^^^^^

The defaults suit a normal home connection. These options change how often
the node checks its reachability and how patient it is with the router:

.. list-table::
   :header-rows: 1
   :widths: 34 12 54

   * - Option
     - Default
     - Description
   * - ``nat-schedule-interval``
     - ``2m``
     - Delay between two reachability checks.
   * - ``nat-num-peers-to-ask``
     - ``3``
     - Peers contacted at each reachability check.
   * - ``nat-max-queue-size``
     - ``3``
     - Past results kept to compute the confidence.
   * - ``nat-min-confidence``
     - ``0.6``
     - Share of those results that must agree before the node trusts them.
   * - ``nat-observed-addr-min-count``
     - ``1``
     - Times an address must be reported by peers before the node uses it.
   * - ``nat-max-relays``
     - ``2``
     - Relays the node reserves a slot on at the same time.
   * - ``nat-port-mapping-discover-timeout``
     - ``500``
     - Milliseconds to wait while looking for a UPnP/NAT-PMP/PCP router.
   * - ``nat-port-mapping-timeout``
     - ``500``
     - Milliseconds to wait while the router creates the mapping.
   * - ``nat-port-mapping-recheck-period``
     - ``300000``
     - Milliseconds between two checks that the mapping is still there.


Ports
~~~~~

Two ports matter for connectivity:

- ``listen-port`` -- the TCP port other peers use to connect to you. The
  default ``0`` picks a random free port. Set a fixed value if you want to
  open it on your router or firewall.
- ``disc-port`` -- the UDP port used to find other peers (default ``8090``).

If you run a reachable node, fix both ports and allow them through your
firewall.

Mix
---

Mix is a privacy layer. When it is enabled, the node hides *who* is asking
for content when it looks up where to find data on the network.

Normally, when a node searches the network to find where some content lives,
the peers it asks can see its identity. With Mix, those lookups are routed
through other relays first, so the peer that answers cannot tell who
originally asked.

.. note::

   Mix is an experimental feature and may change before mainnet.

Set ``mix-enabled`` to ``true``. Mix needs a few extra options to know which
relays it can use:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Option
     - Description
   * - ``mix-enabled``
     - Turn Mix on (default ``false``).
   * - ``dht-mix-proxy``
     - Peer records (SPRs) used as proxy destinations for lookups.
   * - ``mix-pool``
     - Path to a JSON file listing the Mix relays.
   * - ``mix-pool-json``
     - The relay list as inline JSON. Takes precedence over ``mix-pool``.

Example:

.. code-block:: json

   {
     "mix-enabled": true,
     "mix-pool": "/path/to/mix-pool.json"
   }

When Mix is configured (``mix-enabled`` true and at least one ``dht-mix-proxy`` set), the
switch defaults to on, so DHT queries are tunnelled from the start. Call
``togglePrivateQueries(false)`` to stop tunnelling and
``togglePrivateQueries(true)`` to resume. Enabling fails if Mix is not
configured; disabling is always allowed. The call returns the previous state.
This affects queries only, not advertisements.

.. note::

   ``togglePrivateQueries`` is a temporary API and will likely be removed
   before mainnet.

.. toctree::
   :maxdepth: 2
   :caption: API Reference
   :hidden:

   api_reference
