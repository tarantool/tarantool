# Dedicated synchronous and asynchronous replication modes

- **Status:** In progress
- **Issue:** [#11248: Dedicated synchronous replication mode][gh-11248]
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
quorum to be commited, if there're sync spaces in the database.

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
this limitation often set `is_sync = true` on every space. Other ones
constantly rebootstrap...

## 2. Proposed solution

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
  doesn't require quorum to be commited, replicasets with 2 replicas can be
  used with it, since fault tolerance (the number of nodes, which can be stopped
  and write will continue to work) of the async cluster is N - 1.

  The async instance must use `election_mode = 'off'`. Other election modes and
  `box.ctl.promote()` are rejected. On a non-anonymous instance
  `box.ctl.demote()` with no owner remains a no-op. A writable instace must
  use `read_only = false`.

  After recovery or bootstrap, every instance must have no limbo owner and no
  unresolved transactions (limbo must be empty). Otherwise, startup fails
  before application writes are enabled. These checks apply to explicit and
  defaulted mode selections. Startup will not clear ownership, discard queued
  transactions, or reinterpret them to satisfy the configuration.

- `'sync'`: Every newly originated transaction is synchronous. Replicas commit
  these transactions in the same order. With a majority quorum and the
  `complete` wait transaction survives leader failover. `wait = 'submit'` can
  be rolled back on failover (or lost, the same way as async transaction with
  replica death). But transaction does require quorum to be commited, only 3+
  nodes setups are usable (and customers mostly cannot afford 3 availability
  zones), since the fault tolerance is floor((N - 1) / 2).

  A writable instance must use `election_mode = 'manual'` or `'candidate'` and
  own the limbo to be an active leader. The `off` and `voter` are still allowed.
  The `box.ctl.demote()` in `off` still writes `DEMOTE` in WAL and clears the
  limbo owner (needed for sync -> async replicaset transition, see part 2.4).

  During replication an incoming transaction whose synchronous status does not
  match the receiver's `replication_mode` stops replication from that upstream
  with an explicit error. A `'sync'` instance rejects asynchronous transactions,
  and an `'async'` instance rejects synchronous transactions (see part 2.3).
  During startup the replication mode of other replicas is checked before data
  is recovered via ballots.

The following election modes are supported under `'new'`:

| Replication mode | `election_mode`                         | Supported role                                            |
| ---------------- | --------------------------------------- | --------------------------------------------------------- |
| `'async'`        | `'off'`                                 | Async writer or RO replica                                |
| `'async'`        | `'manual'`, `'candidate'`, or `'voter'` | Rejected, error                                           |
| `'sync'`         | `'candidate'`                           | Auto election and synchronous leadership                  |
| `'sync'`         | `'manual'`                              | Synchronous leadership through promote                    |
| `'sync'`         | `'voter'`                               | Voting replica; cannot become a writer                    |
| `'sync'`         | `'off'`                                 | Read-only replica; promote is rejected, demote is allowed |

The `compat.box_cfg_replication_mode = 'new'` makes `replication_mode` option
active and non-dynamic (it's dynamic and inactive under `old`, see 2.5) and
deprecates `is_sync` everywhere (see part 2.2). The compat is dynamic.

All instances in a replicaset, including read-only and anonymous replicas, must
use the same effective `replication_mode` and compatibility selection. The
configuration module must validate this and require these options to be set at
global or replicaset scope. Applications using `box.cfg()` directly must deploy
consistent settings on every instance. Otherwise replication will stop on first
transaction.

### 2.2. Deprecating explicit synchronous flags

Under `'old'`, `is_sync` works as before. If any space has `is_sync = true`,
log one deprecation warning per instance run, pointing users to
`replication_mode`. There are no other `is_sync` warnings.

Under `'new'`, both modes completely ignore stored `is_sync` flags in `_space`.
Stored `true`, `false`, and an absent flag are equivalent. Neither startup nor
mode changes require users to edit them. During the 5.0 schema upgrade,
`box.schema.upgrade()` removes every `is_sync` entry from `_space`, including
both boolean values and system spaces, while preserving other options. Until
5.0 user can still revert the compat to old, so they should be preserved.

Reject every explicit `is_sync` argument as an unknown option in both modes,
including `true` and `false`. This applies to `box.schema.space.create()`,
`space:alter()`, `box.begin()`, `box.commit()`, and `box.atomic()`. Omitting
the argument makes new replicated transactions follow the global mode. The
change of flags through explicit replaces is still available.

Apply the same rule to `IPROTO_IS_SYNC` in `BEGIN` and `COMMIT` requests.
Keep the key recognized, including in 5.0, so it produces an error instead of
being silently skipped as an unknown protocol key.

Under `'new'`, `box_txn_make_sync()` (which is public) makes an active
transaction fail at commit before reaching WAL in either mode (if it not
affects perf that much, and public call only, ofc). Keep such behavior through
4.x and remove the public function in 5.0.

Keep `space.is_sync` and `space.state.is_sync` under `'old'` only. Under
`'new'`, these properties are absent; use `box.cfg.replication_mode`.

Compat defaults to `'old'` in 3.x and `'new'` in 4.0. In 5.0, remove the legacy
behavior in code and mark the compat entry `obsolete = '5.0.0'`: reject
`'old'`, but keep accepting `'new'` and `'default'`.

### 2.3 Replication

We should report a wrong mode before spending hours on recovery. It also
matters for failover: we cannot allow replicas in the replicaset be in the
different replication modes, since after failover it's possible that all
replication become async and split brain will happen.

#### 2.3.1. Checking modes before recovery

Add `replication_mode` to the ballot. Send the configured value or its default
when `compat.box_cfg_replication_mode = 'new'`; omit it under `'old'`. The
applier already receives the first non-empty ballot before recovery. Extend
this exchange to include and check the mode. No extra messages are needed, and
the upstream does not have to finish its own recovery first.

If both nodes report a mode, the values must match, regardless of version. If
either omits the field, allow the connection. The missing field means legacy
behavior: an older version or compat at `'old'`. It does not mean `'async'`.
During startup, a mismatch must fail `box.cfg()` before loading a snapshot or
replaying WAL, even if other peers have matching modes. Once the instance is
running, stop only the affected applier.

#### 2.3.2. Checking incoming transactions

Matching modes do not rule out older transactions written before migration.
Also it's possible, that `compat` is changed dynamically and no applier
reconnect will be done in that case. So, it's proposed to check each
transaction's recorded flags before applying it::::

- A `'sync'` node rejects asynchronous data transactions (`WAIT_ACK` must
  present).
- An `'async'` node rejects synchronous data transactions and records that
  establish a limbo owner (neither `IPROTO_PROMOTE` nor transactions with
  `WAIT_SYNC/ACK` are accepted).

On a mismatch, stop the applier without applying the transaction or advancing
the vclock.

### 2.4. Migration and backward compatibility

Changing `replication_mode` requires downtime: user will have to stop the whole
replicaset before starting any node with the new configuration.

#### 2.4.1. Common preparation (changing replicatin_mode)

First, stop all writers. Wait for running transactions to finish and for the
limbo to become empty. Keep the current leader available until this is done.
Then let all replicas catch up. Their vclocks must match for every replicated
component (excluding the zero component ofc) This makes sure that all nodes
have applied the old transactions before they start using the new rules.
Resolve any replication errors before switching.

#### 2.4.2. Asynchronous to synchronous

After the preparation in section 2.4.1:

1. Set `replication_mode = 'sync'` in every node's configuration.
   Set the election modes as described in section 2.1.
2. Stop all nodes, then restart them with the new configuration.
3. With automatic elections, wait for the elected leader to become the limbo
   owner. With `election_mode = 'manual'`, call `box.ctl.promote()` on the
   intended leader, either manually or through a failover.

#### 2.4.3. Synchronous to asynchronous

After the preparation in section 2.4.1:

1. Make all nodes read-only and set `election_mode = 'off'` while they still
   run in `'sync'`. Keep external promotions disabled.
2. Call `box.ctl.demote()` on the limbo owner. With elections off, this writes
   a `DEMOTE` record that releases ownership. If it fails an ownership or term
   check, keep writes paused and resolve the error before continuing.
3. Wait for every replica to apply `DEMOTE` and the preceding records. Check
   that all nodes have `box.info.synchro.queue.owner == 0` and an empty queue.
4. Save `replication_mode = 'async'` and `election_mode = 'off'` on every node.
   Configure which nodes can accept writes (`read_only`, probably failover).
   Stop all nodes, then restart them with these settings.
5. Wait for all nodes to recover and catch up before resuming writes.

With YAML, use these temporary settings at replicaset scope for step 1:

```yaml
replication:
  mode: sync
  failover: 'off'
  election_mode: 'off'
database:
  mode: ro
```

Keep `mode: sync` explicit here, so disabling failover doesn't also select
`'async'` before the restart.

`box.ctl.demote()` with elections off must remain available under `'new'`,
including in 5.0, because this procedure needs it. After demotion, the nodes
are still in synchronous mode and cannot accept replicated writes. Both
transitions keep compat at `'new'` throughout.

### 2.5 Upgrade process

Upgrade can be done without downtime with rolling upgrade, as always. Compat
can change at runtime in both directions in 3.x and 4.x. Save the final
settings in the startup configuration as well.

From `'old'` to `'new'`:

0. The application should not use `is_sync` flags.
1. Prepare every writer for the chosen workload. For `'sync'`, use one
   synchronous leader and make all replicated spaces synchronous, including
   system spaces and `_sequence_data`. Use the election settings in section
   2.1. For `'async'`, remove synchronous flags and explicit synchronous
   transaction requests, disable elections and external promotions, finish
   pending synchronous transactions, and call `box.ctl.demote()` on the owner.
   Let replicas apply earlier incompatible transactions and any `DEMOTE`.
   Compatible writes can continue.
2. Set the same `replication_mode` on every instance while compat is `'old'`.
   Validate the mode, election settings, and current queue state immediately:
   `'async'` requires no limbo owner and no unresolved entries. Reject invalid
   settings without changing the stored value. On success, store the value
   and log a warning that it takes effect only with compat at `'new'`.
   Replication still follows the legacy rules.
3. Set `compat.box_cfg_replication_mode = 'new'` on every instance, including
   the master.

From `'new'` to `'old'` (unavailable in 5.0):

1. Check that every replicated space would keep the current replication mode
   under `'old'`. Use the stored `is_sync` flags in `_space`.
2. Set `compat.box_cfg_replication_mode = 'old'` on every instance, including
   the master. Recheck the spaces before each switch. Per-space rules become
   effective and `replication_mode` becomes inactive, but writes retain their
   sync or async behavior.
3. Keep the workload and leadership settings compatible with the previous mode
   until all nodes use `'old'`. An incompatible write would stop replication on
   nodes still using `'new'`. Once all nodes have switched, legacy configuration
   changes can resume.

Under `'new'`, reject runtime changes to `replication_mode`; setting the same
value is allowed. Neither compat transition may reinterpret in-flight
transactions, and failed validation must leave the settings unchanged.

Binaries without these options still need a rolling upgrade after preparing the
workload (point 1 `old` -> `new`).

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

* `'sync'` to `'async'`: disable elections and external promotions, resolve
  pending synchronous transactions, and replicate `DEMOTE`. Every node must
  have an empty queue and no owner before asynchronous writes begin.

The replication_mode, election_mode and failover settings must be corrdinated.
It doesn't look like Tarantool's work for me, moreover, it seems, that
`sync/async` is a one time solution for the cluster. This all requires draining
writes, difficult synchronization, softening the replication checks (part 2.3).
This all looks impossible to me.

Instead we can use the `mixed` mode as described above, this will work, but I
see no motivation to support that mode.

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

PostgreSQL can require rewind even with synchronous acknowledgments; MongoDB
can automatically undo writes already acknowledged by the old primary. So,
everyone do whatever they want.

The details about every database and source links are [here][other-dbs].

--------------------------------------------------------------------------------

[other-dbs]: https://gist.github.com/Serpentian/0574f4bf38e933e695f035c1f39d7112
[gh-11248]: https://github.com/tarantool/tarantool/issues/11248
[tntp-7050]: https://jira.vk.team/browse/TNTP-7050
[tntp-9077]: https://jira.vk.team/browse/TNTP-9077

