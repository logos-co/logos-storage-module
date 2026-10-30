# CLAUDE.md

@README.md

## Rules

- A node restart goes through `destroy()` then `init()`, never `stop()` then
  `start()`. libp2p does not support restarting a `Switch`: the node comes
  back broken.
- A change to the saved config is a new `migrateVNtoVN+1` step in
  `src/storage_module_plugin.cpp`, with a `configVersion` bump. The option
  names and defaults follow `conf.nim` in `logos-storage-nim`. The defaults
  table in `docs/index.rst` follows them too.

## Reviews

A review reports only the bugs the change brings. A change that breaks a
rule of the Rules section is a bug.

Each bug names the case that triggers it. Nothing else: no style, no
wording, no guess, no risk without a case. When nothing is found, the
review says "Nothing found."

`lib/libstorage.h` and `conf.nim` are not in this repository. Never guess
what they hold.

These are not bugs:

- a caller that breaks the API contract
- a context not freed when a dispatch fails: libstorage calls the callback
  before the dispatch returns, and `asyncCallback` frees the context
- a `SyncCtx` the syncCall wrappers free when a dispatch fails:
  `syncCallback` has already run and did not free it
- a `SyncCtx` not freed on a timeout: its callback frees it when it fires

On a pull request, each bug is a comment on the changed line that causes
it. The summary comment is a checklist: one box per bug, one sentence, and
a link to its line comment.
