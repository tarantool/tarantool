# Dedicated synchronous and asynchronous replication modes

- **Status:** Complete
- **Start date:** 23-09-2026
- **Authors:** Nikita Zheleztsov @serpentian <n.zheleztsov@proton.me>,
               Serge Petrenko @sergepetrenko <sergepetrenko@tarantool.org>,
               Vladislav Shpilevoy @gerold103 <v.shpilevoy@tarantool.org>
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
API (either `box.begin/commit/atomic` or `IPROTO_IS_SYNC`).

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
rebootstrap.

Users, who want at least one synchronous space, should make all spaces
synchronous and just use `wait = submit` in order not to wait for commit to
happen. Such transactions with `sumbmit` can be rolled back on failover.

## 2. Proposed solution

### 2.0. Summary

Newly added:

* Dynamic `compat.box_cfg_replication_mode`, which preserves mixed replication
  in `'old'`, in `'new'` - enables dedicated modes and deprecates `is_sync`.
  Defaults: `'old'` in 3.x, `'new'` in 4.0. In 5.0 - remove legacy behavior.

* Dynamic `box.cfg.replication_mode` (`replication.mode` in YAML) selects
  `'sync'` or `'async'` (default) for all replicated transactions under `'new'`.
  Changing the mode requires a pause in writes and disabled failover.

* Check modes in ballots on every connection and reconnect, and check all
  incoming transactions before applying them, `async` rejects `PROMOTE`.

* `box.ctl.demote({clear_owner = true})` to clear the limbo owner and write
  the `DEMOTE` in WAL. Needed for migration from sync replicaset to async.

Breaking changes under `'new'`:

* User cannot mix sync and async in the same replicaset anymore. Replication
  can be either sync or async, it's configured with `replication_mode`,
  separate spaces cannot be done sync or async anymore.

* `space.<name>.is_sync` and `space.<name>.state.is_sync` are removed.

* Reject every explicit `is_sync` argument as an unknown option in the
  following functions: `box.schema.space.create()`, `space:alter()`,
  `box.begin()`, `box.commit()`, and `box.atomic()`.

* Under `'new'`, `box_txn_make_sync()` is a no-op in `'sync'` and aborts the
  active transaction in `'async'`. It is removed from exports in 5.0.

* `IPROTO_IS_SYNC` is rejected with error, the feature is also removed
   from `iproto_features`.

* `box.ctl.promote()` with `election_mode = 'off'` is rejected.

### 2.1. Configuration interface

Add the compatibility option `compat.box_cfg_replication_mode` and the
configuration option `box.cfg.replication_mode` (see alternative 3.1 for
implicit sync/async mode instead of the cfg option). Expose the latter as
`replication.mode` in YAML configuration.

`replication_mode` accepts exactly two values: `'sync'` and `'async'` (see
alternative 3.2 for the `'mixed'` option value). `'async'` is the default.
The option is dynamic; changing modes is described in section 2.4. Alternative
3.3 considers requiring a restart instead.

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
  But the replicaset with `N` nodes tolerates `floor((N - 1) / 2)` failures
  while continuing to accept writes, so in order to survive death of 1 replica
  at least 3 instances are needed.

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

Setting `compat.box_cfg_replication_mode = 'new'` activates `replication_mode`
and deprecates per-space replication modes. Explicit `is_sync` arguments are
forbidden (see section 2.2). Both options are dynamic. Under `'old'`, the mode
can be prepared for a later compat switch, but does not affect transactions
(see section 2.5).

Outside a migration, all instances in a replicaset, including read-only and
anonymous replicas, must use the same effective mode and compat selection.
The configuration module must validate the desired configuration and require
these options at global or replicaset scope. Applications using `box.cfg()`
directly must deploy consistent settings themselves. Section 2.3 describes
checks on connections and incoming transactions, which enforce the same mode in
Tarantool itself.

### 2.2. Deprecating per-space replication modes

Under `'old'`, `is_sync` works as before. If any space has `is_sync = true`,
log one deprecation warning per instance run, pointing users to
`replication_mode`. There are no other `is_sync` warnings.

Under `'new'`, both modes completely ignore stored `is_sync` flags in `_space`.
Stored `true`, `false`, and an absent flag are equivalent. Neither startup nor
mode changes require users to edit them. Until 5.0 user can still revert the
compat to old, so that these flags are in use again.

For transactions with `txn_isolation = 'linearizable'`, space access under
`'new'` depends on `replication_mode`: replicated spaces are allowed in
`'sync'` and rejected in `'async'`, regardless of stored `is_sync` flags.
Local and data-temporary spaces remain allowed in either mode.

Reject every explicit `is_sync` argument as an unknown option in both modes,
including `true` and `false`. This applies to `box.schema.space.create()`,
`space:alter()`, `box.begin()`, `box.commit()`, and `box.atomic()`. Omitting
the argument makes new replicated transactions follow the global mode. The
change of flags through explicit replaces is still available.

Apply the same rule to `IPROTO_IS_SYNC` in `BEGIN` and `COMMIT` requests.
Keep the key recognized, including in 5.0, so it produces an error instead of
being silently skipped as an unknown protocol key. The IPROTO feature for it
also depends on compat.

Under `'new'`, the public C API `box_txn_make_sync()` is a no-op in `'sync'`.
In `'async'`, it marks the active transaction TXN_ABORTED. The public function
is removed from exports in 5.0.

Kgep `space.is_sync` and `space.state.is_sync` under `'old'` only. Under
`'new'`, these properties are absent; use `box.cfg.replication_mode`.

Deprecate `failover.replicasets.<name>.synchro_mode` in YAML configuration.
Under `'new'`, reject any explicit value for this option. Use
`replication.mode` instead to select the supervised failover mode.

Compat defaults to `'old'` in 3.x and `'new'` in 4.0. In 5.0, remove the legacy
behavior in code and mark the compat entry `obsolete = '5.0.0'`: reject
`'old'`, but keep accepting `'new'` and `'default'`.

### 2.3. Replication checks

#### 2.3.1. Ballots

Add `replication_mode` to the ballot. Send its configured value or default
under `'new'`; omit it under `'old'`.

Check the mode on every connection and reconnect. If both nodes report a mode,
the values must match, regardless of version. If either omits the field, allow
the connection: it means legacy behavior, not `'async'`.

At startup, compare the first non-empty ballots before loading the snapshot,
replaying local WAL, or building indexes. An explicit mismatch must fail
`box.cfg()`, even if other peers match. An unavailable peer or a missing mode
cannot be validated at this stage.

During operation, a mismatch blocks only the affected applier. Retry the
connection so replication can resume once the modes agree. This can happen
after a peer restart or compat change, including in a topology without reverse
connections. A temporary peer mismatch must not prevent changing the local
mode during migration.

Changing mode or compat does not restart established replication connections.
The existing ballot watcher stops at SUBSCRIBE; keeping it alive is not
required. Incoming records are checked independently.

#### 2.3.2. Incoming records

Under `'new'`, check every incoming replicated data transaction using its WAL
flags, including transactions from legacy peers. Apply the same checks during
final JOIN and SUBSCRIBE.

* In `'sync'`, require `WAIT_ACK`.
* In `'async'`, reject `WAIT_SYNC` or `WAIT_ACK`. Also reject any `PROMOTE`
  that establishes a limbo owner, including in bootstrap metadata.

Check before applying records. If mode or compat changes while applying a
data transaction, recheck its flags before WAL. On a mismatch, roll back any
uncommitted changes and stop the applier. This error is not retryable: the
incompatible record is already in the sender's WAL.

Snapshot rows represent existing data, not transactions, so the transaction
flag check does not apply to them. Local WAL recovery also preserves the
original transaction semantics: a valid history can contain both modes and a
`PROMOTE` followed by `DEMOTE`. After local recovery, enforce the state
requirements in section 2.1.

### 2.4. Changing `replication_mode`

The option is dynamic. Use the same checks when changing the mode under
`'new'` and when switching compat from `'old'` to `'new'`. The setter must:

1. Validate the mode and requested election settings together. Require
   `'off'` for async and anonymous replicas, and `'manual/voter/candidate'`
   for registered sync nodes.
2. When entering async, reject ongoing promotions or demotions.
3. Scan the global `txns` list. Reject the change if any replicated
   `TXN_PREPARED` transaction has final WAL flags incompatible with the target
   mode: sync requires `WAIT_ACK`, async permits neither `WAIT_SYNC` nor
   `WAIT_ACK`. Compatible prepared transactions may finish normally.
4. When entering async, require the limbo owner to be zero.

Run the checks and activate the mode without yielding. On failure, leave the
settings unchanged. Switching compat to `'new'` must still run the checks.

`TXN_INPROGRESS` transactions do not block the switch. Local transactions use
the mode active at commit. If a transaction explicitly requested sync before
the switch, fail its commit before WAL in async mode. Replicated transactions
keep the sender's flags and follow the checks in section 2.3, including a
recheck before WAL if mode or compat changed while applying them.

These checks are local; the user, ATE, config storage or smth else must
coordinate the replicaset.

#### 2.4.1. Common preparation

1. Stop application writes and external failover. It's recommended to set
   `read_only = true` to avoid any intenal writes (e.g. PITR point generator).
   On registered sync nodes, use `'manual'` or `'voter'`. the owner should be in
   `'manual'`. Async and anon nodes keep `'off'`. Stop manual promotions too.
2. Finish application transactions, wait for empty limbo, and call
   `box.ctl.wal_sync()` on every node (recommended).
3. Query `box.info.vclock` on every node. Continue only when all replicated
   components match, ignoring component zero. Do not skip unavailable nodes
   (recommended).

Matching vclocks are necessary but not sufficient: they do not prove that
transactions have finished or prevent new writes and promotions. Keep writes
and promotions stopped until every node has switched.

If user skips that stage and mid-switch replica gets incompatible transaction,
replication will be broken.

#### 2.4.2. Asynchronous to synchronous

After the common preparation:

1. Save and apply `replication_mode = 'sync'` on every node, together with
   `election_mode = 'manual'` or `'voter'` on registered nodes and `'off'` on
   anonymous replicas. The intended leader must use `'manual'`.  (using
   `candidate` or external manual promotion during this phase may break
   replication to the nodes which are not yet in `'sync'` state)
2. Wait until every node has switched and replication is connected.
3. Promote the intended leader or restore automatic/external failover. Once
   the leader owns the limbo, resume application writes.

#### 2.4.3. Synchronous to asynchronous

We add `box.ctl.demote({clear_owner = true})`. The caller must use `'manual'`
or `'off'`, own the limbo in the current term. The call writes a replicated
`DEMOTE` and resigns leadership. Ordinary `demote()` keeps its current
behavior.

After the common preparation:

1. Call `box.ctl.demote({clear_owner = true})` on the owner. Wait for every
   node to apply `DEMOTE`: owners must be zero, queues empty, and fresh
   vclocks equal apart from component zero.
2. Save and apply `replication_mode = 'async'` with `election_mode = 'off'`
   on every node.
3. Wait until every node has switched and replication is connected. Restore
   the intended writers and async failover, then resume application writes.

### 2.5. Upgrade process

A replicaset with an already compatible workload can switch to `'new'` while
writes continue. For mixed workloads, either prepare compatible writes under
`'old'` or pause them during the switch.

Save the chosen mode and compat in every node's startup configuration. When
upgrading to 4.x, set compat to `'old'` explicitly if the application is not
ready for `'new'` yet.

From `'old'` to `'new'`:

1. Prepare the application for section 2.2. Calls to nodes using `'new'` must
   omit all `is_sync` arguments, including `IPROTO_IS_SYNC`.
2. Choose how to prepare the workload. To keep writes running, all replicated
   transactions must already match the target mode, including per-transaction
   requests and system-space writes. If needed, change `is_sync` in `_space`
   while compat is still `'old'`. Finish earlier incompatible transactions
   and wait for every replica to apply them before switching the first node.
   Otherwise, pause writes and promotions and follow section 2.4.1. Keep them
   paused until every node uses `'new'`; stored flags need not be changed in
   that case.
3. Prepare ownership and election settings. For a running sync workload, keep
   its leader and owner, using the roles in section 2.1. For a paused
   transition to sync, use `'manual'` or `'voter'` on registered nodes and
   `'off'` on anonymous replicas. Establish leadership only after every node
   has switched. For async, clear ownership if needed, replicate `DEMOTE`
   everywhere as in section 2.4.3, and use `'off'`.
4. Set and save `replication_mode` on every node. Run all checks in section
   2.4 against the target mode before storing it. Under `'old'`, the mode
   remains inactive and existing transaction rules still apply. Warn that it
   takes effect only with `'new'`.
5. Set and save compat to `'new'` on every node. Repeat all checks against
   the configured mode: transactions or promotions may have started since
   step 4. If validation fails, leave compat unchanged. With a prepared
   workload this can be done one node at a time while writes continue.

The setter checks local state only. Users must ensure peers still using
`'old'` produce compatible transactions and earlier incompatible ones have
replicated. Transactions in progress follow the commit rules in section 2.4.
Incoming transaction checks apply even when the sender still uses `'old'`.
A missing mode in its ballot does not bypass them.

From `'new'` to `'old'` (unavailable in 5.0):

1. Prepare the stored `is_sync` flags and application behavior that will
   become active under `'old'`. To keep writes running, they must continue
   producing transactions compatible with every node still using `'new'`.
   Use direct replacements in `_space` if needed and wait for replication;
   `space:alter({is_sync = ...})` is rejected under `'new'`.
2. If the restored behavior will be mixed or use another mode, stop writes
   and promotions and prepare the replicaset as in section 2.4 before changing
   compat. Keep writes paused until every node has switched.
3. Set and save compat to `'old'` on every node. Only then restore the intended
   legacy workload and failover. Explicit `is_sync` arguments can be used
   again only on nodes already running with `'old'`.

A compat round-trip is not a shortcut around section 2.4. Incoming records are
always checked against the receiver's active mode under `'new'`, and ballot
checks still run on every connection and reconnect.

### 2.6. Replication failures during a switch

During a mode change or compat upgrade, any reconnect may find a ballot
mismatch while nodes use different modes. The applier retries until modes
agree; this does not prevent applying the local configuration.

If writes or promotions continue, an incompatible WAL record can stop an
applier. The record stays in the sender's WAL, so reconnecting alone cannot
fix it. Losing ACKs may also block synchronous writes.

To recover:

1. Stop writes and promotions across the replicaset.
2. Align modes if the entire pending WAL is compatible with one mode.
   Otherwise, in 3.x/4.x, temporarily return affected nodes to compat `'old'`.
3. Restart stopped appliers, let replicas catch up, and resolve pending
   transactions. Repeat the prepared switch from section 2.4 or 2.5.

In 5.0, `'old'` is unavailable. A mixed backlog may require rebootstrap the
affected replicas, though, it's difficult to get one, it seems.

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

The `mixed` value could instead allow receiving both sync and async
transactions while changing modes with writes still running. For the writes
to work here `mixed` must support all `election_mode` values. The `sync` mode
must require `is_sync = true` for spaces, the `async` -> `is_sync = false/nil`,
the flag must be preserved everywhere and endlessly.

Allowing anonymous replicas to use `mixed` would avoid reconfiguring them
during a switch, but would keep mixed replication in an otherwise dedicated
replicaset.

### 3.3. Require a restart to change modes

The previous proposal made `replication_mode` non-dynamic under `'new'` and
required stopping the whole replicaset before changing it.

A restart removes active transactions and recreates replication connections,
but the preparation in section 2.4 can establish the same boundary. It does
not remove the need to stop writers and promotions or synchronize replicas.
Also, dynamic compat would allow `'new' -> 'old' -> change mode -> 'new'`
without a restart unless this path had additional restrictions.

Use the same preparation and validation for runtime changes instead of
requiring a restart.

### 3.4. Keep the `is_sync` for app compatibility

There was idea not to break applications and allow `is_sync` partially. But
this means we won't be able to drop it at all and we'll always have these
checks in code, even when `is_sync` is not used at all anymore. It was decided
to break the app one time and call it a day. Moreover, it's easy to fix it:
just drop all `is_sync` in code and in migrations.

Deprecate per-space replication modes, but keep `is_sync` as a compatibility
NoOp argument. Section 2.2 describes removing the argument completely.

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
`'new'`, these properties are removed completely; use
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
section 2.2), new compat will have to be introduced.

### 3.5. Allow `election_mode = 'off'` in synchronous mode

The prev proposal allowed registered sync replicas to use `'off'`. They
would still vote and count toward quorum, but could not be promoted under
`'new'`. `box.ctl.demote()` would keep clearing ownership in this mode.

For sync to async migration, users would stop writes, drain the queue, and set
every node to `'off'`. The owner would then call ordinary `box.ctl.demote()`.
After all replicas applied `DEMOTE` and became ownerless, users would switch
the replicaset to `replication_mode = 'async'`.

This avoids adding an argument to `demote()`, but makes `'off'` another voting
role with special migration behavior, `voter` is preferable here. And making
registered `'off'` nodes non-voting while still counting them toward quorum
could prevent elections.

### 3.6. Check only ballots

Checking modes only when connecting would allow incompatible transactions
from peers using `'old'`, which omit the mode. A mode or compat change also
does not restart established connections.

Section 2.3 therefore checks incoming records as well. Rolling upgrades must
prepare a compatible workload or pause writes, rather than accept conflicting
transaction modes during the rollout.

The option became dynamic, checking transactions only on connect is not enough
anymore.

### 3.7. Store the mode in a replicated system space

Store the mode in `_schema`. This makes it explicit and carries it through
snapshots and WAL to all replicas, including anonymous and joining ones.

However, this changes configuration semantics:

* Setting `replication_mode` through `box.cfg()` becomes a replicated
  transaction, allowed only on a writable master. In sync mode it requires
  the Raft leader and quorum; a read-only replica cannot initiate the change.
* The initial mode must be persisted before activating the new behavior.
  Updates must use the old mode for their own WAL write and coordinate
  activation with `PROMOTE`/`DEMOTE`. This needs a special transition protocol
  to avoid making the mode change depend on its own result.
* Concurrent async writers still need coordination: one writer's WAL does
  not order their writes. Election settings can only be checked against the
  final stored mode after recovery.

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

PostgreSQL can require manual rewind even for synchronously acknowledged
transactions; MongoDB can automatically undo writes already acknowledged by the
old primary. So, everyone does whatever they want.

The details about every database and source links are [here][other-dbs].

--------------------------------------------------------------------------------

[other-dbs]: https://gist.github.com/Serpentian/0574f4bf38e933e695f035c1f39d7112
[gh-11248]: https://github.com/tarantool/tarantool/issues/11248
[gh-12273]: https://github.com/tarantool/tarantool/issues/12273
[tntp-7050]: https://jira.vk.team/browse/TNTP-7050
[tntp-9077]: https://jira.vk.team/browse/TNTP-9077
