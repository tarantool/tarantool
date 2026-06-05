## Tarantool

Tarantool is a DB with an application server on board.

The product is considered a low-level framework for building actual databases,
as it provides the strong basics: storage engines in-memory and on-disk,
replication (synchronous and asynchronous), transaction journal, network API
(IProto protocol), C-compatible API with Lua API built on top of it.

The runtime of the DB is threads having specific roles with cooperative
multitasking inside almost all the threads.

- WAL thread for journal writes.
- TX thread for work with the data and transaction execution.
- IProto thread pool for networking.
- Applier threads for receiving replication from an upstream.
- Relay threads for sending replication to a downstream. Thus relays send data
  to appliers.

The cooperative multitasking is implemented using cord module, which runs an
event-loop in a thread, and the coroutines (fibers) are running in this event
loop. The library also provides various synchronization and communication means
for these fibers: condvars, channels, explicit wakeup, sleeps, yields, and more.

### Storage engines

There are 2 storage engines: memtx and vinyl. Their sources can be found in
`src/box/` across multiple files having specific prefixes `memtx_...` and
`vy_...`.

Memtx is an in-memory engine which has various index types such as hash, B-tree,
R-tree. Vinyl is an on-disk engine which uses LSM trees for indexes.

Each engine has its own transaction manager. The managers are joined together
by an upper level API so cross-engine transactions are possible.

### Replication

Replication in Tarantool works via the journal. Transactions get written into
the journal, and then from there are sent to remote replicas via relay objects
(`src/box/relay.cc` is the core).

On the other side the replica receives the data using an applier object
(`src/box/applier.cc`).

Relay -> applier is a unidirectional channel. For cross-replication the
instances establish 2 channels relay -> applier and applier -> relay, which in
turn is often used to build fullmesh replication in a replicaset.

The replication can be asynchronous and synchronous. Asynchronous is just
transactions being sent eventually, and the originator of the txns doesn't wait
for their replication.

Synchronous replication makes the transaction author wait until the transaction
is confirmed by a quorum of nodes until it can be committed and its changes
become visible. It is implemented using Raft protocol:
- Base election library in `src/lib/raft`.
- Tarantool wrapper around Raft in `src/box/raft.*`.
- Synchronous transactions queue in `src/box/txn_limbo*`.

### Structure

The project is highly modular and values performance and simplicity, in this
order, over everything else. The performance becomes secondary only in code
which is not on hot paths.

The main language is C and is the primary language for all internal new code.
Lua is used for public non-perf-critical API, and uses LuaJIT under the hood.
C++ remains in some places, is not growing, but can still be used without STL.

- `src/` is all the sources.
- `test/` is all the tests.
- `src/box/` is storage-aware code which is Tarantool-specific.
- `src/lib/` are storage-agnostic self-sufficient libraries.
- `src/lua/` are storage-agnostic Lua modules.

Orphan files directly in `src/` are mostly like `src/lib/` and things related to
the process management.

### Testing

Tests are entirely located in `test/`. There are several ways of testing,
depending on what is being tested. The most widely used ones are listed below.

- C/C++ unit tests. They are handy for covering C APIs which don't require the
  full storage being configured. Such tests use TAP library, and are located in
  `test/unit/`. They are fast, simple, and offer very low-level access to
  internals of Tarantool.

- Lua Diff tests. These are legacy tests, their files all end with `.test.lua`.
  Code in such Lua files is executed like in an interactive console, the correct
  output is saved into `.result` files, and on each run the actual output is
  compared to the `.result` file. They must match. New tests do not use this
  method anymore.

- Lua Luatest tests. These are the majority of all tests. They all end with
  `_test.lua`, and are usually located in folders `test/*-luatest`, grouped by
  what they are testing - application server, fibers, replication, SQL, a
  specific engine, etc. These are the default way of testing unless it is
  possible to write a C/C++ unit test.

Testing usually tries to cover 100% of new code. When some cases are very tricky
to cover, sometimes the tests use error injections - a debug-build option added
right in the code which allows to simulate an error or a delay, which are very
hard to achieve using only public APIs. See `src/lib/core/errinj*` and its
usages, if you need more.

### Task-specific guidance

The files under `doc/agents/` hold deeper, task-specific knowledge - the
agent-agnostic equivalent of Claude Code "skills". Each has a description below.

**Do not read these files up front.** Load a file into context only once your
current task matches its description, and only the one(s) that match. You can
make this decision without opening the files first, by looking at the
descriptions here.

- `doc/agents/general-dev.md` - Enables standards for writing and reviewing
  Tarantool patches: commit structure and message format, rules for modularity,
  performance, comments, and code style. MUST use when writing or editing code
  and committing.

- `doc/agents/replication-dev.md` - Enables knowledge for replication-specific
  tasks. MUST use when developing or reasoning about replication overall,
  applier, relay, txn_limbo, raft, synchronous and asynchronous replication,
  replicas.

- `doc/agents/replication-test.md` - Enables writing and running tests for
  replication-specific code. MUST use when editing/adding tests under
  `test/replication/` or `test/replication-luatest/`.

- `doc/agents/setup-dev.md` - Enables access to user-specific development setup:
  sources fetch, configure, build, tests, static analysis. MUST use when
  `doc/agents/local_setup.md` is missing or seems outdated, and you need to do
  any of the previously listed activities.

These descriptions are copied from the `description` field of the matching
Claude skill files in `.claude/skills/*/SKILL.md`. If at some point you open one
such skill file and notice its description differs from the copy above, it must
be flagged to the user.

---

If anything in the overview seems outdated from how the code actually works,
then it must be immediately flagged to the user.
