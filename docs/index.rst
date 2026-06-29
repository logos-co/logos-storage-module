Logos Storage Module
====================

The Logos Storage Module lets your application share files over a
peer-to-peer (p2p) network.

It is a Logos platform module written in C++. Under the hood it wraps
``libstorage``, the storage engine built from `logos-storage-nim
<https://github.com/logos-storage/logos-storage-nim>`_.

What it does
------------

- **Upload a file** and get back a **CID** (Content Identifier). A CID is a
  short text that uniquely identifies the content.
- **Share the CID** with anyone. With the CID, any node on the same network
  can **download** the file.
- **Manage local data**: list what you store, check whether content exists,
  remove it, or see how much space you use.

Because content is addressed by its CID, the same file always has the same
CID, and you always get back exactly what was uploaded.

How you use it
--------------

A typical flow is:

1. ``start`` -- create the node from a JSON configuration and join the network.
2. ``uploadUrl`` / ``downloadToUrl`` -- send and receive files.
3. ``stop`` -- shut down cleanly.

See the `Tutorial
<https://logos-co.github.io/logos-doctest-hub/#logos-storage-module/ubuntu-latest/running-this-storage-module-against-logoscore>`_
for a full example, and the :doc:`API Reference <api_reference>` for every
method.

Configuration
-------------

You configure a node by passing a JSON string to ``start``. Every key is
optional: any key you leave out keeps its default value.

The options below are the ones you are most likely to need. For the full
list with default values, see the ``start`` method in the
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
     - ``any``
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
     "nat": "any",
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

An unreachable node still works in one direction: you can download content
from other nodes, but they cannot download from you.

.. tip::

   Automatic NAT traversal (hole punching) is coming. It will let nodes
   behind a router reach each other without manual port mapping.

The ``nat`` option controls how the node tries to become reachable from the
internet:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Value
     - When to use it
   * - ``any``
     - Default. Tries the methods below automatically.
   * - ``none``
     - No NAT traversal: the node announces the machine's own IP as-is. Use
       this when the machine already has a public IP (e.g. a cloud server or
       VPS). With only a private IP, the node stays unreachable.
   * - ``upnp``
     - If your router has UPnP enabled, the node asks it to open a port so
       you become reachable from the internet.
   * - ``pmp``
     - Same as ``upnp``, but using NAT-PMP. Use it when your router supports
       NAT-PMP instead.
   * - ``extip:<IP>``
     - Set your public IP yourself, e.g. ``extip:203.0.113.7``. Use this when
       you know your public IP and have opened your listen port on the router
       yourself.

.. note::

   Some Linux distributions (such as Fedora) enable a firewall by default
   that can block incoming connections even when your port mapping is
   correct. You may need to allow the port through the firewall.


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
   :caption: Reference
   :hidden:

   api_reference
