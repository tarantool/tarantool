# Dedicated synchronous and asynchronous replication modes

- **Status:** In progress
- **Issue:** [#11248][gh-11248] and [#12273][gh-12273]
- **Related:** [TNTP-7050][tntp-7050] and [TNTP-9077][tntp-9077]
- **Target:** Tarantool 3.9

This document proposes the replication mode option: either all transactions are
synchronous or all are asynchronous. We should also preserve the existing mixed
behavior by default in Tarantool 3.x.

## 1. Problem description

Tarantool currently lets users configure synchronous replication separately for
each space through the `is_sync` option. A transaction that writes only to
async spaces is usually asynchronous. Writing to at least one sync space makes
the transaction synchronous. A user can also explicitly request sync
replication for an individual transaction through `is_sync` in the transaction
API (either `box.begin/atomic` or `IPROTO_IS_SYNC`).

An async transaction can wait behind an earlier sync transaction in the
synchronous transaction queue (aka limbo). Any async transaction can basically
become sync, if the limbo is not empty, so we cannot give guarantees, that the
transaction over async space will be fast and furious or that it won't require
quorum to be committed, if there're sync spaces on the instance.

Mixing these transactions becomes a problem during a leader change:

1. Instance A owns the limbo and accepts an async write.
2. A commits the write locally and reports success to the client. Instance B
   has not received the transaction yet.
3. B becomes the new leader and claims the limbo in a newer term.
4. B receives the async transaction originating from A's older term.

The applier detects this situation and reports `SPLIT_BRAIN` with the reason
`got an async transaction from an old term`. Accepting the transaction could
merge conflicting histories; discarding it could lose a write for which the
client has already received a successful commit response. The error protects
consistency, but stops replication and requires user intervention.

Consequently, leader failover in production with mixed sync and async writes
frequently causes `SPLIT_BRAIN` errors during normal operation. Users who know
this limitation set `is_sync = true` on every space. Other ones constantly
rebootstrap...

## 2. Proposed solution

### 2.0. Resume

Newly added things:

* Dynamic `compat.box_cfg_replication_mode`, which preserves mixed replication
  in `'old'`, in `'new'` - enables dedicated modes and deprecates `is_sync`.
  Defaults: `'old'` in 3.x, `'new'` in 4.0. In 5.0 - remove legacy behavior.

* `box.cfg.replication_mode` (`replication.mode` in YAML) selects `'sync'` or
  `'async'` (default) for all replicated transactions. It takes effect under
   `'new'`. Changing it requires a restart.

* `box.ctl.demote({{clear_owner = true}})` to clear the limbo owner and write
  the `DEMOTE` in wal. Needed for migration from sync replicaset to async.

Breaking changes under `'new'`:

* User cannot mix sync and async in the same replicaset anymore. Replication
  can be either sync or async, it's configured with `replication_mode`,
  separate spaces cannot be done sync or async anymore.

* `space.<name>.is_sync` and `space.<name>.state.is_sync` are removed.

* Reject every explicit `is_sync` argument as an unknown option in the
  following functions: `box.schema.space.create()`, `space:alter()`,
  `box.begin()`, `box.commit()`, and `box.atomic()`.

* `box_txn_make_sync()` in C API becomes NoOp in 4.X under `new`. It's
  completely removed from exports in 5.0.

* `IPROTO_IS_SYNC` is rejected with error, the feature is also removed
   from `iproto_features`.

### 2.1. Configuration interface

Add the compatibility option `compat.box_cfg_replication_mode` and the
configuration option `box.cfg.replication_mode` (see alternative 3.1 for
implicit sync/async mode instead of the cfg option). Expose the latter as
`replication.mode` in YAML configuration.

`replication_mode` accepts exactly two values: `'sync'` and `'async'` (see
alternative 3.2 for the `'mixed'` option value). `async` is default, the option
is non-dynamic (see alternative 3.3 for dynamic) and requires restart to be
changed (see part 2.4 for the workflow of switching the option):

- `'async'` (default): Every newly originated transaction is asynchronous with
  all the consequences: order of transactions can differ on replicas in the
  replicaset, writing the transaction doesn't guarantee that you can read it a
  second later on master if failover happened, replication conflicts are
  possible, so the application must be aware of async replication or use
  conflict resolution triggers (`before_replace`). The transaction can be lost
  after commit (if replica dies completely and didn't replicate it). But it
  doesn't require quorum to be committed, replicasets with 2 replicas are often
  used with async, since fault tolerance (the number of nodes, which can be
  stopped and write will continue to work) of the async cluster is N - 1.

  The async instance must use `election_mode = 'off'`. Other election modes and
  `box.ctl.promote()` are rejected. On a non-anonymous instance
  `box.ctl.demote()` with no owner remains a no-op. A writable instance must
  use `read_only = false`.

  After recovery or bootstrap, every instance must have no limbo owner, no
  unresolved transactions (limbo must be empty). Otherwise, startup fails.
  These checks apply even to defaulted async mode. Startup will NOT clear
  ownership, discard queued transactions, or reinterpret them to satisfy the
  configuration.

- `'sync'`: Every newly originated transaction is synchronous. Replicas commit
  these transactions in the same order. With a majority quorum and the
  `complete` wait transaction survives leader failover. `wait = 'submit'` can
  be rolled back on failover (or lost, the same way as async transaction with
  replica death). Transactions still need the configured quorum to commit.
  But the replicaset with `N` nodes tolerate `floor((N - 1) / 2)` failures
  while continuing to accept writes, so in order to survive death of 1 replica
  at least 3 instance are needed.

  A writable instance must use `election_mode = 'manual'` or `'candidate'`,
  own the limbo, and have `read_only = false`. Setting `read_only = true`
  blocks application writes even on the leader without changing leadership (as
  it's now).

  Registered replicas may also use `'voter'`. Reject `'off'` on these nodes
  (see alternative 3.5). Anonymous replicas still require `'off'`: they cannot
  vote or become leaders and do not count toward quorum.

  Add `box.ctl.demote({clear_owner = true})` to release the limbo owner for
  migration (see section 2.4.3). Without this option, `box.ctl.demote()` keeps
  its current behavior: with elections enabled, it resigns leadership but
  leaves the limbo owner in place.

The following election modes are supported under `'new'`:

| Replication mode | `election_mode`                         | Supported role                             |
| ---------------- | --------------------------------------- | ------------------------------------------ |
| `'async'`        | `'off'`                                 | Async writer or RO replica                 |
| `'async'`        | `'manual'`, `'candidate'`, or `'voter'` | Rejected                                   |
| `'sync'`         | `'candidate'`                           | Auto election and synchronous leadership   |
| `'sync'`         | `'manual'`                              | Synchronous leadership through promote     |
| `'sync'`         | `'voter'`                               | Voting replica; cannot become a writer     |
| `'sync'`         | `'off'`                                 | Anon RO replica; `replication_anon = true` |

The `compat.box_cfg_replication_mode = 'new'` makes `replication_mode` option
active and non-dynamic (it's dynamic and inactive under `old`, see 2.5) and
deprecates per-space replication modes. The `is_sync` arguments are forbidden
from now on (see part 2.2).  The compat is dynamic.

All instances in a replicaset, including read-only and anonymous replicas, must
use the same effective `replication_mode` and compatibility selection. The
configuration module must validate this and require these options to be set at
global or replicaset scope. Applications using `box.cfg()` directly must deploy
consistent settings on every instance. Tarantool itself checks that only at
start (see part 2.3), as the `replication_mode` is non-dynamic.

### 2.2. Deprecating per-space replication modes

Under `'old'`, `is_sync` works as before. If any space has `is_sync = true`,
log one deprecation warning per instance run, pointing users to
`replication_mode`. There are no other `is_sync` warnings.

Under `'new'`, both modes completely ignore stored `is_sync` flags in `_space`.
Stored `true`, `false`, and an absent flag are equivalent. Neither startup nor
mode changes require users to edit them. Until 5.0 user can still revert the
compat to old, so that these flags are in use again.

Reject every explicit `is_sync` argument as an unknown option in both modes,
including `true` and `false`. This applies to `box.schema.space.create()`,
`space:alter()`, `box.begin()`, `box.commit()`, and `box.atomic()`. Omitting
the argument makes new replicated transactions follow the global mode. The
change of flags through explicit replaces is still available.

Apply the same rule to `IPROTO_IS_SYNC` in `BEGIN` and `COMMIT` requests.
Keep the key recognized, including in 5.0, so it produces an error instead of
being silently skipped as an unknown protocol key. The IPROTO feature for it
also depends on compat.

Under `'new'`, `box_txn_make_sync()` (which is public C API) is NoOp. It's
dropped from exports in 5.0.

Keep `space.is_sync` and `space.state.is_sync` under `'old'` only. Under
`'new'`, these properties are absent; use `box.cfg.replication_mode`.

Compat defaults to `'old'` in 3.x and `'new'` in 4.0. In 5.0, remove the legacy
behavior in code and mark the compat entry `obsolete = '5.0.0'`: reject
`'old'`, but keep accepting `'new'` and `'default'`.

### 2.3 Ensure one mode for replicaset

Tarantool should check, that all instances in replicaset use the same
`replication_mode`. Since the option is non-dynamic it's enough to do that only
on replication "startup". We should report a wrong mode before spending hours
on recovery So it's proposed to use ballots for that (see the alternative 3.6
for the checks inside SUBSCRIBE).

Add `replication_mode` to the ballot. Send the configured value or its default
when `compat.box_cfg_replication_mode = 'new'`; omit it under `'old'`. The
applier already receives the first non-empty ballot before recovery. Extend
this exchange to include and check the mode.

If both nodes report a mode, the values must match, regardless of version. If
either omits the field, allow the connection. The missing field means legacy
behavior: an older version or compat at `'old'`. It does not mean `'async'`.
During startup, a mismatch must fail `box.cfg()` before loading a snapshot or
replaying WAL, even if other peers have matching modes. Once the instance is
running, stop only the affected applier (though, I dunno how it can happen).

### 2.4. Changing `replication_mode`

Changing `replication_mode` requires downtime: user will have to stop the WHOLE
replicaset before starting any node with the new configuration. Not rolling
restart, stop of the whole replicaset.

#### 2.4.1. Common preparation

First, stop all writers. Wait for running transactions to finish and for the
limbo to become empty. Then let all replicas catch up. Their vclocks must match
for every replicated component (excluding the zero component ofc) This makes
sure that all nodes have applied the old transactions before they start using
the new rules. Resolve any replication errors before switching.

#### 2.4.2. Asynchronous to synchronous

After the preparation in section 2.4.1:

1. Set `replication_mode = 'sync'` in every node's configuration.
   Set the election modes as described in section 2.1.
2. Stop all nodes, then restart them with the new configuration.
3. With automatic elections, wait for the elected leader to become the limbo
   owner. With `election_mode = 'manual'`, call `box.ctl.promote()` on the
   intended leader, either manually or through a failover.

#### 2.4.3. Synchronous to asynchronous

We add the `box.ctl.demote({clear_owner = true})` for this transition. The
caller must use `election_mode = 'manual'`, own the limbo in the current term,
and have an empty queue. The operation writes a replicated `DEMOTE` record and
resigns leadership. Ordinary `box.ctl.demote()` keeps its current behavior.

After the preparation in section 2.4.1:

1. Save and apply temporary settings: make all nodes read-only and set
   `election_mode = 'manual'` on every registered node. This disables automatic
   elections while preserving the current leader. Anonymous replicas keep
   `'off'`. Keep external promotions disabled (disable failover!).
2. Call `box.ctl.demote({clear_owner = true})` on the limbo owner.
3. Wait for every replica to apply `DEMOTE` and the preceding records. Check
   that all nodes have `box.info.synchro.queue.owner == 0` and an empty queue.
4. Stop all nodes. Set `replication_mode = 'async'` and `election_mode = 'off'`
   in every node's startup configuration and configure which nodes can accept
   writes. Restart all nodes with these settings.
5. Wait for all nodes to recover and catch up before resuming writes.

With YAML, use these temporary settings at replicaset scope for step 2:

```yaml
replication:
  mode: sync
  failover: 'off'
  election_mode: manual
database:
  mode: ro
```

### 2.5 Upgrade process

Binaries can be upgraded one at a time, including from 4.x with compat at
`'old'` to 5.0 with compat at `'new'`. In 3.x and 4.x, compat can also change
at runtime in both directions. The switch itself does not need a restart,
but the preparation below may require pausing writes.

Save the chosen mode and compat in every node's startup configuration. When
upgrading to 4.x, set compat to `'old'` explicitly if the application is not
ready for `'new'` yet: the default changes in 4.0. Before upgrading to 5.0,
also update and rebuild C modules that refer to `box_txn_make_sync()`, which
is no longer exported there.

From `'old'` to `'new'`:

1. Prepare the application for the API changes in section 2.2. Calls to nodes
   using `'new'` must omit all `is_sync` arguments, including `IPROTO_IS_SYNC`.
   While a node still uses `'old'`, removing an explicit `is_sync = true` may
   turn a synchronous transaction into an asynchronous one. The application
   must handle both behaviors during the rollout, or writes must pause while
   the application and compat are switched together.
2. While compat is still `'old'`, prepare the chosen mode:
   - For `'sync'`, use `'candidate'`, `'manual'`, or `'voter'` on registered
     nodes according to their roles. Anonymous replicas keep `'off'`. The
     writable leader must own the limbo and use `'candidate'` or `'manual'`.
   - For `'async'`, finish pending synchronous transactions and clear the limbo
     owner, if any: `box.ctl.demote({clear_owner = true})`. Wait for every
     replica to apply `DEMOTE`. Check that `box.info.synchro.queue.owner == 0`
     and `box.info.synchro.queue.len == 0` everywhere. Then set `election_mode =
     'off'`.
3. Set `replication_mode = 'sync'` or `'async'` on every instance. Under
   `'old'`, this setting can change at runtime and is validated, but
   replication still follows the old rules. There is no need to change the
   stored `is_sync` flags for either mode, they're ignored in `new`.
4. Set `compat.box_cfg_replication_mode = 'new'` on every instance.

The switch affects new local transactions. Transactions already in progress
keep their sync or async behavior. Replicas apply incoming transactions with
the sync or async flags recorded in WAL, including transactions from nodes
still using `'old'`. Existing split-brain checks continue to apply.

From `'new'` to `'old'` (unavailable in 5.0):

1. Check the stored `is_sync` flags: they become active again under `'old'`.
   A space that was synchronous under the global mode may become asynchronous,
   or vice versa If needed use direct replacements in `_space` and wait for
   every replica to apply them. `space:alter({is_sync = ...})` is rejected
   while compat is `'new'`.
2. Set `compat.box_cfg_replication_mode = 'old'` on every instance. Per-space
   rules become active and `replication_mode` stops taking effect.
3. Restore application code that uses explicit `is_sync` arguments only after
   the nodes receiving those calls have switched to `'old'`. Once all nodes
   use `'old'`, other legacy configuration changes can resume.

Switching back also leaves transactions already in progress unchanged.

Under `'new'`, reject runtime changes to `replication_mode`; setting the same
value is allowed. Changing between the dedicated modes follows section 2.4.
Ballot checks run when replication connects or reconnects. Changing compat
does not restart replication, so users must apply the same final settings to
all nodes without relying on an immediate ballot check.

## 3. Considered alternatives

### 3.1. Select the mode from limbo ownership

With this approach, all replicated spaces would act synchronously while
`box.info.synchro.queue.owner` is nonzero and asynchronously while it is zero.
It is close to the existing `compat.box_consider_system_spaces_synchronous`
mechanism and requires fewer configuration choices.

However, I don't like how the cluster starts here: a writable instance can
accept async transactions before `promote()` is called (which can again lead to
split brains). An async deployment also lacks a declaration that would let the
server reject accidental promotion. The explicit mode makes both intentions
enforceable before writes begin.

### 3.2. Expose `sync`, `async`, and `mixed`

An explicit `replication_mode = 'mixed'`, used as the default in 3.x, could
preserve the current behavior instead of the compat. This is an
alternative interface for the same approach. The compat is better, since we
have the strategy for deprecating it and removing all the code related to `old`.
The `bootstrap_strategy = legacy` won't probably be ever deleted from tarantool
and we'll have to support code for it endlessly, don't like that.

The current behavior (`mixed`) doesn't work: it produces split brains if
`promote` is called with `election_mode = off` or if sync and async spaces are
mixed. I'd avoid supporting this.

Though the `mixed` value can be used in order to make the `replication_mode`
dynamic, so that in this mode it's allowed to receive both sync and async
replication. For the writes to work here `mixed` must support all
`election_mode` values. The `sync` mode must require `is_sync = true` for
spaces, the `async` -> `is_sync = false/nil`, the flag must be preserved
everywhere and endlessly.

Maybe anon replicas need mixed value, so that they are not reconfigured on
switch. But the switch never happens, and nobody use the tarantool as anon
replica (they're mostly reimplemented).

### 3.3. Dynamic cfg option

Allow switching between `'sync'` and `'async'` at runtime:

* `'async'` to `'sync'`: stop the old writers and let replicas reach a
  known vclock boundary before promoting a synchronous leader. All replicas
  in the replicaset must have the same vclock, when sync replication will be
  enabled.

* `'sync'` to `'async'`: disable automatic elections and external promotions,
  resolve pending synchronous transactions, and replicate `DEMOTE`. Every node
  must have an empty queue and no owner before asynchronous writes begin.

The replication_mode, election_mode and failover settings must be corrdinated.
It doesn't look like Tarantool's work for me, moreover, it seems, that
`sync/async` is a one time solution for the cluster. This all requires draining
writes, difficult synchronization, softening the replication checks (part 2.3).
This all looks impossible to me.

Instead we can use the `mixed` mode as described above, this will work, but I
see no motivation to support that mode.

### 3.4. Keep the `is_sync` for app compatibility

There was idea not to break applications and allow `is_sync` partially. But
this means we won't be able to drop it at all and we'll always have these
checks in code, even when `is_sync` is not used at all anymore. It was decided
to break the app one time and call it a day. Moreover, it's easy to fix it:
just drop all `is_sync` in code and in migrations, that is it:

Deprecate per-space replication modes, but keep `is_sync` as a compatibility
NoOp argument. Alternative 3.4 describes removing the argument completely.

Under `'old'`, `is_sync` works as before. But if any space has `is_sync =
true`, we log one deprecation warning per instance run, pointing users to
`replication_mode`.

Under `'new'`, `box.schema.space.create()` and `space:alter()` accept:

| Mode      | `is_sync = true` | `is_sync = false` |
| --------- | ---------------- | ----------------- |
| `'sync'`  | Accepted         | Accepted          |
| `'async'` | Error            | Accepted          |

In `sync` mode passing `is_sync = false` doesn't make a space async anymore,
it's always sync (as it is for `box_consider_system_spaces_synchronous`, where
the space is async in `_space`, but it's implicitly sync), but in 4.0 the flag
in `_space` is still written. In `'async'`, accepting `is_sync = true` would
silently weaken the requested guarantees, so it's prohibited and causes error.

`box.begin()`, `box.commit()`, and `box.atomic()` accept `is_sync = true` in
`sync` mode (`is_sync = false` is error now and it'll remain error in any
mode). Passing `is_sync = true` in `async` mode causes error from now on, we
cannot fulfill these guarantees. The same applies to `IPROTO_IS_SYNC` in
`BEGIN` and `COMMIT`.

Under `'new'`, `box_txn_make_sync()` (which is public) is completely NoOp
(similar to the `fiber_set_cancellable` deprecated C API function).

Keep `space.is_sync` and `space.state.is_sync` under `'old'` only. Under
`'new'`, these properties are removed compleltely; use
`box.cfg.replication_mode`.

Through 4.x, keep existing space flags in `_space` and store accepted flags
from space creation and alteration, so users can revert to `'old'`. In
`'sync'`, these stored values do not affect replication. In `'async'`, reject
stored `is_sync = true` flags (again, we cannot fulfill these guarantees in
that mode). Before enabling `'async'`, check the final recovered schema and
report spaces whose flags must be cleared. Recovery must still accept
historical flags in snapshots and WAL.

Compat defaults to `'old'` in 3.x and `'new'` in 4.0. In 5.0, remove the legacy
replication behavior and mark the compat entry `obsolete = '5.0.0'`: reject
`'old'`, but keep accepting `'new'` and `'default'`. The 5.0 schema upgrade
removes every `is_sync` entry from `_space`.

In 5.0, the API will look strange: `alter()` accepts `is_sync`, validates it,
but doesn't write it to `_space`:

```lua
-- With replication_mode = 'async':
space:alter({is_sync = true})  -- Error: sync guarantees are unavailable.
space:alter({is_sync = false}) -- Accepted; no is_sync is stored in _space.
```

If we'll want to drop the `is_sync` completely (as it's described in
alternative 3.4), new compat will have to be introuduced.

### 3.5. Allow `election_mode = 'off'` in synchronous mode

The prev proposal allowed registered sync replicas to use `'off'`. They
would still vote and count toward quorum, but could not be promoted under
`'new'`. `box.ctl.demote()` would keep clearing ownership in this mode.

For sync to async migration, users would stop writes, drain the queue, and set
every node to `'off'`. The owner would then call ordinary `box.ctl.demote()`.
After all replicas applied `DEMOTE` and became ownerless, the whole replicaset
would restart with `replication_mode = 'async'`.

This avoids adding an argument to `demote()`, but makes `'off'` another voting
role with special migration behavior, `voter` is preferable here. And making
registered `'off'` nodes non-voting while still counting them toward quorum
could prevent elections.

### 3.6 Checking incoming transactions in SUBSRIBE

Initially it was poroposed to ensure the mode for every transaction inside
SUBSCRIBE. But firstly, it doesn't allow fluent `old` -> `new` migration and
requires all spaces to be changed with proper `is_sync` flag in `_space`. And
it also doesn't make sense to do that, since the option is non-dynamic:

Matching modes do not rule out older transactions written before migration.
Also it's possible, that `compat` is changed dynamically and no applier
reconnect will be done in that case. So, it's proposed to check each
transaction's recorded flags before applying it.

- A `'sync'` node rejects asynchronous data transactions (`WAIT_ACK` must be
  present).
- An `'async'` node rejects synchronous data transactions and records that
  establish a limbo owner (neither `IPROTO_PROMOTE` nor transactions with
  `WAIT_SYNC/ACK` are accepted).

On a mismatch, stop the applier without applying the transaction or advancing
the vclock.

## 4. Experience from other databases

| Database          | Replication model                           | Scope                                       | Old master has a transaction missing on the new master                                |
| ----------------- | ------------------------------------------- | ------------------------------------------- | ------------------------------------------------------------------------------------- |
| PostgreSQL        | Sync or async WAL replication               | Session or transaction                      | Cannot rejoin without rewind/rebuild; divergent data is discarded.                    |
| MongoDB           | Async oplog + configurable replica ACK wait | Write operation or transaction              | Normally rolls back automatically, including `w: 1` writes.                           |
| Redis Open Source | Async + optional post-write ACK/fsync wait  | Barrier after writes on a client connection | Sentinel resynchronizes the old master; extra writes are discarded.                   |
| MySQL             | Async or semisync (classic replication)     | Source and replica configuration            | Manual recovery; discard/rebuild the divergent old source.                            |
| SQL Server        | Sync or async commit                        | Availability replica configuration          | Old database stays suspended; manual resume rolls back extra writes.                  |
| YTsaurus          | Sync and async table replicas               | Table replica mode; per-write sync guard    | No table-master promotion; queue catch-up is required before switching to sync.       |
| Spanner           | Sync Paxos quorum; other replicas may lag   | Paxos quorum per split                      | Committed data preserved; pending work recovered or aborted automatically.            |
| CockroachDB       | Sync Raft quorum; async non-voting replicas | Raft quorum per range                       | Committed data preserved; automatic log repair and transaction recovery.              |
| YugabyteDB        | Sync Raft quorum within the primary cluster | Raft quorum per tablet                      | Committed data preserved; conflicting uncommitted log entries replaced automatically. |
| TiDB/TiKV         | Sync TiKV quorum; async TiFlash replicas    | Raft quorum per Region                      | Committed data preserved; automatic log repair and pending-lock resolution.           |

PostgreSQL can require manual rewind even for synchronously acknowledgmented
transaction; MongoDB can automatically undo writes already acknowledged by the
old primary. So, everyone do whatever they want.

The details about every database and source links are [here][other-dbs].

--------------------------------------------------------------------------------

[other-dbs]: https://gist.github.com/Serpentian/0574f4bf38e933e695f035c1f39d7112
[gh-11248]: https://github.com/tarantool/tarantool/issues/11248
[gh-12273]: https://github.com/tarantool/tarantool/issues/12273
[tntp-7050]: https://jira.vk.team/browse/TNTP-7050
[tntp-9077]: https://jira.vk.team/browse/TNTP-9077
