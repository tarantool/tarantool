# Dedicated synchronous and asynchronous replication modes

- **Status:** Complete
- **Start date:** 23-09-2026
- **Authors:** Nikita Zheleztsov @serpentian <n.zheleztsov@proton.me>,
               Vladislav Shpilevoy @gerold103 <v.shpilevoy@tarantool.org>
               Serge Petrenko @sergepetrenko <sergepetrenko@tarantool.org>,
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
synchronous and just use `wait = 'submit'` in order not to wait for commit to
happen. Such transactions with `submit` can be rolled back on failover.

## 2. Proposed solution

### 2.0. Summary

Newly added:

* Dynamic `compat.box_cfg_replication_mode`, which preserves mixed replication
  in `'old'`, in `'new'` - enables dedicated modes and deprecates `is_sync`.
  Defaults: `'old'` in 3.x, `'new'` in 4.0. In 5.0 - remove legacy behavior.

* Dynamic `box.cfg.replication_mode` (`replication.mode` in YAML) selects
  `'sync'` or `'async'` (default) for all replicated transactions under `'new'`.
  Changing the mode requires a pause in writes to replicated spaces (temporary
  and local are still allowed, RO load is also allowed) and disabled failover.

* Check modes in ballots on every connection and reconnect. Under `'new'`,
  check incoming transactions and rows only if their originator is known to
  use `'new'`. Skip these record checks for old or unknown originators.

* `box.ctl.demote({clear_owner = true})` to clear the limbo owner with a
  replicated `DEMOTE`, as ordinary `demote()` does in `election_mode = 'off'`.
  The option enables this behavior in every election mode for sync to async
  option migration.

* `box.ctl.wal_sync({include_synchro_queue = true})` to wait for WAL writes
  of prepared transactions, including those waiting for synchronous queue
  space. The option defaults to `false`, preserving current behavior.

* `box.ctl.replicaset_sync({timeout = N, include_anon = B})` to wait for every
  replica in `_cluster` (and possibly for connected anon) to reach the caller's
  vclock captured at the start of the call.

Breaking changes under `'new'`:

* User cannot mix sync and async in the same replicaset anymore. Replication
  can be either sync or async, it's configured with `replication_mode`,
  separate spaces cannot be done sync or async anymore.

* `space.<name>.is_sync` and `space.<name>.state.is_sync` are removed.

* Reject every explicit `is_sync` argument as an unknown option in the
  following functions: `box.schema.space.create()`, `space:alter()`,
  `box.begin()`, `box.commit()`, and `box.atomic()`.

* `box_txn_make_sync()` aborts the active transaction. It is removed from
  exports in 5.0 completely.

* `IPROTO_IS_SYNC` is rejected with an error, the feature is also removed
  from `iproto_features`.

* `box.ctl.promote()` with `election_mode = 'off'` is rejected.

* `failover.replicasets.<name>.synchro_mode` is rejected in YAML config.

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
  `complete` wait, a transaction survives leader failover. `wait = 'submit'` can
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

  Add `box.ctl.demote({clear_owner = true})` for the limbo owner to clear
  ownership during migration (see section 2.4.2). Replicas, including anonymous
  ones, clear ownership when they apply its `DEMOTE`. Without this option,
  `box.ctl.demote()` keeps its current behavior: with elections enabled, it
  resigns leadership but leaves the limbo owner in place.

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

Under `'old'`, `is_sync` works as before, without new deprecation warnings (see
alternative 3.8). Under `'new'`, both modes completely ignore stored `is_sync`
flags in `_space`. Until 5.0 user can still revert the compat to old, so that
these flags are in use again.

For transactions with `txn_isolation = 'linearizable'`, space access under
`'new'` depends on `replication_mode`: replicated spaces are allowed in
`'sync'` and rejected in `'async'`, regardless of stored `is_sync` flags.
Local and data-temporary spaces remain allowed in either mode.

Under `'new'`, reject every explicit `is_sync` argument as an unknown option in
both modes, including `true` and `false`. This applies to
`box.schema.space.create()`, `space:alter()`, `box.begin()`, `box.commit()`,
and `box.atomic()`. Omitting the argument makes new replicated transactions
follow the global mode. The change of space flags through explicit replaces is
still available.

Apply the same rule to `IPROTO_IS_SYNC` in `BEGIN` and `COMMIT` requests.
Keep the key recognized, including in 5.0, so it produces an error instead of
being silently skipped as an unknown protocol key. The IPROTO feature for it
also depends on compat.

Under `'new'`, the public C API `box_txn_make_sync()` marks the active
transaction `TXN_ABORTED`. The public function is removed from exports in 5.0.

Keep `space.is_sync` and `space.state.is_sync` under `'old'` only. Under
`'new'`, these properties are absent; use `box.cfg.replication_mode`.

Deprecate `failover.replicasets.<name>.synchro_mode` in YAML configuration.
Under `'new'`, reject any explicit value for this option. Use
`replication.mode` instead to select the supervised failover mode. With
`replication.failover = 'supervised'`, setting `replication.mode = 'sync'`
selects the same failover behavior as `failover.replicasets.<name>.synchro_mode
= true` did previously.

Compat defaults to `'old'` in 3.x and `'new'` in 4.0. In 5.0, remove the legacy
behavior in code and mark the compat entry `obsolete = '5.0.0'`: reject
`'old'`, but keep accepting `'new'` and `'default'`.

### 2.3. Replication checks

#### 2.3.1. Ballots

Add `replication_mode` to the ballot. Send its configured value or default
under `'new'`; omit it under `'old'`. The presence of this field indicates
that the remote node uses the new compat behavior.

This check is needed for fast replicaset check during node start. At startup,
check the modes advertised in ballots received during initial replication
connections, before loading the snapshot, replaying local WAL, or building
indexes. An explicit mismatch must fail `box.cfg()`. An unavailable peer or a
missing mode cannot be validated at this stage.

Check the mode on every connection and reconnect. If both nodes report a mode,
the values must match, regardless of version. If either omits the field, allow
the connection: it means legacy behavior, not `'async'`.

During operation, a mismatch blocks only the affected applier. Retry the
connection so replication can resume once the modes agree. This can happen
after a peer restart or compat change. A temporary peer mismatch must not
prevent changing the local mode during migration.

Changing mode or compat does not restart established replication connections.
The existing ballot watcher stops at SUBSCRIBE. Incoming record checks (see
2.3.2) use the latest known ballot of the transaction's originator. Its compat
change becomes known when a connection to that originator refreshes its ballot.

#### 2.3.2. Incoming records

Under `'new'`, check incoming replicated data transactions using their WAL
flags only if their originator advertises `replication_mode` in its latest
known ballot. Identify the originator by the row's `replica_id`, including when
another node relays the transaction (so in chain `M -> R1 -> R2` the `R2` never
checks transactions of `M`). Apply the same condition during final JOIN and
SUBSCRIBE:

* In `'sync'`, require `WAIT_ACK` unless all transaction rows are `IPROTO_NOP`.
* In `'async'`, reject `WAIT_SYNC` or `WAIT_ACK`. Also reject any `PROMOTE`
  that establishes a limbo owner, including in bootstrap metadata.

If the originator uses `'old'`, or its identity or compat behavior is unknown,
skip these additional record checks and use the existing replication rules.
An upgraded relay may therefore forward legacy transactions without causing
a mode check failure. Preserve their WAL flags and transaction semantics
regardless of the receiver's mode.

On a mismatch, reject the incoming transaction or row (e.g. PROMOTE) before
applying it or writing it to WAL, and stop the affected applier. Do not advance
the applied vclock for the rejected record. This check does not roll back
existing limbo entries or other transactions.

The error is not retryable: the incompatible record is already in the sender's
WAL, so retrying won't fix the problem. After fixing the problem (e.g. by
setting the proper mode), manually restart the affected applier.

These checks are not applied to snapshot or xlog rows. Though, after local
recovery the requirements in 2.1 must be satisfied.

### 2.4. Changing `replication_mode`

The mode can change at runtime without restart, but it requires downtime in
writes (if user wants no downtime and is smart enough, then 3.x/4.x still has
that option via transition to `'old'`, changing mode and then to `'new'`). RO
requests can continue.

The following checks are applied on switch of the `replication_mode` and on
switch of the `compat.box_cfg_replication_mode` to `'new'`:

1. Check mode and election settings together: async and anonymous replicas
   require `'off'`; registered sync nodes require `'manual'`, `'voter'`, or
   `'candidate'`.
2. Before entering async, require no limbo owner, empty limbo and no unfinished
   promotions or demotions.

These checks are local. The caller must coordinate the nodes. There are no
checks for transactions in progress, replication status, or vclocks of connected
replicas: the switch of mode happens like never, so no need to protect it that
much: the worst failure here is replication breakage.

These helpers are available under both compat settings:

- Extend `box.ctl.wal_sync()` with an optional options table. The boolean
  `include_synchro_queue` defaults to `false`: omitting it or passing `false`
  keeps the existing behavior. With `true`, include prepared transactions
  waiting for synchronous queue space at the start of the call, submit them
  to WAL, and wait for their WAL writes to complete. This uses the
  non-rotating path of `txn_journal_flush()`. It does not wait for open
  transactions that have not entered commit, or guarantee confirmation or
  replication; the limbo may remain non-empty. Waiting for queue space can
  still require earlier transactions to be confirmed. Reject unknown options
  and non-boolean values. Alternative 3.9 discusses the API naming.
- `box.ctl.replicaset_sync({timeout = N, include_anon = B})` captures the
  caller's `box.info.vclock` at the start of the call and waits for every
  replica in `_cluster` (and possibly for connected anon) to reach it, ignoring
  component zero. Do not skip unavailable nodes. It does not flush local
  transactions or wait for later writes.
- `box.ctl.demote({clear_owner = true})` uses the existing demotion behavior of
  `election_mode = 'off'` in every election mode, under either compat setting.
  Anonymous callers are rejected; with no owner, the call on a registered node
  is a no-op. Replicas, including anonymous ones, clear ownership when they
  apply the `DEMOTE` record. The call does not wait for the `DEMOTE` itself to
  reach replicas or gather quorum ACKs. In `'candidate'`, the node can be
  reelected.

#### 2.4.1. Asynchronous to synchronous

1. Stop writes and external failover. Set `read_only = true` on every node
   to block background writes too.
2. Call `box.ctl.wal_sync()` on every node. After all calls finish, call
   `box.ctl.replicaset_sync()` on every node and wait for all calls to finish.
3. Set `replication_mode = 'sync'` on every node, together with
   `election_mode = 'manual'` on the intended leader, `'manual'` or `'voter'`
   on other registered nodes, and `'off'` on anonymous replicas.
4. Wait until every node has switched and replication is connected.
5. Promote the leader or restore automatic/external failover. Once the
   leader owns the limbo, make it writable again and resume writes.

**Failures and recovery**

With this preparation, a temporary ballot mismatch during step 3 is the only
expected mode check failure. Runtime appliers retry until the modes agree. At
startup, `box.cfg()` fails instead: fix the settings and retry startup.

Without catch-up, pending async WAL can stop appliers after the switch. Keep
writes and promotions stopped. Align settings with the pending WAL, or use
`'old'` on affected nodes in 3.x/4.x (`'old'` not available in 5.x). Manually
restart the stopped appliers, let replicas catch up, and repeat the procedure.
Reconnecting without fixing the incompatible mode does not help.

Promoting before async WAL from other writers reaches the replicas can cause
`SPLIT_BRAIN`. Returning to `'old'` won't help. Rebootstrap the affected
replica.

#### 2.4.2. Synchronous to asynchronous

1. Stop writes, external failover, and manual promotions. Use `'manual'` on
   the leader, `'manual'` or `'voter'` on other registered nodes, and `'off'`
   on anonymous replicas. Set `read_only = true` everywhere.
2. On every node, call `box.ctl.wal_sync({include_synchro_queue = true})`,
   then wait for `box.info.synchro.queue.len == 0`.
   The empty-queue wait includes confirmation or rollback; flushing alone
   does not finish synchronous transactions.
3. After these waits finish everywhere, call `box.ctl.replicaset_sync()` on
   every node and wait for all calls to finish. This also replicates
   `CONFIRM` and `ROLLBACK` records before any ownership is cleared.
4. On the leader, call `box.ctl.demote({clear_owner = true})`, then
   `box.ctl.replicaset_sync({include_anon = true})`. Wait for it to finish:
   every replica must apply `DEMOTE` and clear its limbo owner.
5. Set `replication_mode = 'async'` with `election_mode = 'off'` everywhere.
6. Wait for every node to switch. Restore writers.

**Failures and recovery**

Again, if done correctly, only a temporary ballot mismatch is possible. If
catch-up is skipped, replication of sync WAL can stop an applier after the
switch. Then manual intervention is required (see 2.4.1, first two paragraphs in
failures).

An election or manual promotion during migration can conflict with `DEMOTE`.
For example, A owns the limbo in term T and writes `DEMOTE(T+1)`, while B wins
an election and writes `PROMOTE(T+1)`. Whichever record reaches a node second
is rejected with `SPLIT_BRAIN`, stopping replication. This can happen even
with an empty limbo: the `DEMOTE` record itself does not require quorum ACKs.

Stop further promotions and choose which node's history to keep. Rebootstrap
the affected replicas from that node. Returning to `'old'` or reconnecting
does not remove the conflicting WAL records.

### 2.5. Upgrade process

You can switch from `'old'` to `'new'` while writes continue. Stored `is_sync`
flags in `_space` need not change.

Prepare the application for nodes using `'new'`: remove `is_sync` arguments,
`IPROTO_IS_SYNC`, `box_txn_make_sync()`, and reads of `space.is_sync` and
`space.state.is_sync`. Replace `failover.replicasets.<name>.synchro_mode`
with `replication.mode`.

#### 2.5.1. Runtime compat switch

Choose one target `replication_mode` (`replication.mode` in YAML) for the
replicaset. With one writer, keep the same master during the switch.

Steps marked `(from mixed)` handle legacy transactions, WAL, or limbo
ownership that do not fit the target mode. If all writes and pending WAL
already match the target mode (and async limbos are empty and ownerless),
skip these steps and switch nodes in any order.

**Switching to sync**

While compat is `'old'`, set `replication_mode = 'sync'` on every node.
Use `election_mode = 'manual'` or `'candidate'` on the leader; other
registered nodes may also use `'voter'`. Anonymous replicas use `'off'`.
Keep the existing leader and limbo owner. If there is no sync leader,
then you probably don't want to migrate to sync.

1. Set `compat.box_cfg_replication_mode = 'new'` on the master.
2. (from mixed) On the master, call
   `box.ctl.wal_sync({include_synchro_queue = true})` to finish WAL writes
   for transactions already prepared under `'old'`. New writes can continue.
3. (from mixed) Call `box.ctl.replicaset_sync()` on the master and wait for
   it to finish.
4. Switch the replicas to `'new'`.
5. (recommended) Restart replication.

**Switching to async**

While compat is `'old'`, set `replication_mode = 'async'` and
`election_mode = 'off'` everywhere. Disable further promotions and wait for
any pending promotion or demotion to finish.

1. (from mixed) On the master, call `box.ctl.demote({clear_owner = true})`
   and wait for it to drain the limbo and clear ownership.
2. Set `compat.box_cfg_replication_mode = 'new'` on the master.
3. (from mixed) On the master, call
   `box.ctl.wal_sync({include_synchro_queue = true})` to finish WAL writes
   for transactions already prepared under `'old'`.
4. (from mixed) Call `box.ctl.replicaset_sync({include_anon = true})` on the
   master and wait for it to finish. Replicas apply the last sync transactions
   and the replicated `DEMOTE`, which clears their limbo owners.
5. Switch the replicas to `'new'`.
6. (recommended) Restart replication.

Async writes can continue. Legacy sync writes may fail after ownership is
cleared; once the master uses `'new'`, all writes are async even if their
spaces still have `is_sync = true`.

The setter to `'new'` checks configuration and, for async, an empty limbo and no
owner or pending promotion/demotion.

**Failures and recovery**

For a sync target, switching all nodes without waiting for catch-up should
also work in most cases: existing connections keep their old ballots until
reconnect and still accept mixed transactions. But transactions prepared
under `'old'` keep their original flags. If the originator's ballot is
refreshed before these transactions are applied, the new checks can stop
the applier.

To recover from an incompatible WAL record in 3.x/4.x:

1. Set affected replicas to `'old'` and manually restart their stopped
   appliers.
2. Once all writers produce transactions in the target mode, call
   `box.ctl.wal_sync({include_synchro_queue = true})` on every node.
   After these calls finish, call `box.ctl.replicaset_sync()` on every node
   and wait for all calls to finish. Writes can continue.
3. Switch the caught-up replicas back to `'new'` and reconnect.

For sync, keep the same leader in `'new'`; it can continue writing during
recovery. Do not wait for empty limbos before restoring replication: missing
ACKs may prevent commits. Sync writes still need quorum. If a writer must
return to `'old'`, keep its writes compatible with the target mode or pause
them.

For an async target, wait for every replica to apply `DEMOTE` before switching
it to `'new'`; otherwise, the local owner check rejects the switch. Concurrent
promotions can conflict with `DEMOTE` as described in section 2.4.2. Changing
the leader while async WAL from other writers remains can also cause
`SPLIT_BRAIN`. Rebootstrap the affected replicas from the chosen history.

#### 2.5.2. Rolling upgrade with restart

Choose the target mode and election settings as in section 2.5.1. Keep
unupgraded nodes in `'old'` unless they have already switched to `'new'`.

For an async target, all limbos must be empty and ownerless, and all writes
and pending WAL must already be async. Otherwise, first complete the async
workflow in section 2.5.1. Nodes already in `'new'` stay there during the
upgrade. Upgrading the binary does not clear ownership.

For a replicaset with one writable master:

1. Pick a read-only replica and restart it on the target Tarantool version
   with the target mode and compat `'new'`. Wait for
   `box.info.status == 'running'`.
2. Restart the other read-only replicas the same way, one at a time. Keep
   enough nodes running to satisfy the sync quorum.
3. Make an upgraded replica the new master: promote it for sync, or transfer
   the writable role for async. Move writes to it and leave the former master
   read-only.
4. (from mixed) On the former master, call
   `box.ctl.wal_sync({include_synchro_queue = true})`, then
   `box.ctl.replicaset_sync()`. The new master can keep accepting writes during
   this wait.
5. Restart the former master as a read-only replica on the target version,
   with the target mode and compat `'new'`. Wait for it to reach `'running'`.
6. Run `box.schema.upgrade()` on the new master. This updates the system
   spaces; the changes are replicated to the other nodes.

**Failures and recovery**

For sync, skipping the wait in step 4 can stop appliers on legacy WAL after
the former master restarts. In 3.x/4.x, set affected replicas to `'old'` and
manually restart their appliers. Keep the writers in `'new'`. On the former
master, call `box.ctl.wal_sync({include_synchro_queue = true})`, then
`box.ctl.replicaset_sync()`. Once the calls finish, switch the affected
replicas back to `'new'` and reconnect.

A sync promotion can still cause `SPLIT_BRAIN` if old async transactions
arrive after the new leader takes over. This also happens under `'old'`;
returning to it does not remove the check.

For an async target, restarting a replica before it applies `DEMOTE` fails
the startup owner check. Let it catch up under its previous settings, then
retry the upgrade. Concurrent promotions can conflict with `DEMOTE` as
described in section 2.4.2.

For `SPLIT_BRAIN`, stop writes or promotions that keep causing the conflict.
If the remaining WAL cannot be applied, affected replicas may need to be
rebootstrapped. In 5.x, there is no `'old'` fallback for incompatible WAL.

#### 2.5.3. Returning to legacy old behavior

In 3.x and 4.x, set compat back to `'old'` on each node. Stored `is_sync`
flags become active again. Prepare the application for that behavior, and
use explicit `is_sync` arguments only after the node has switched.

Choose the order based on the writes after the switch:

- If transactions remain compatible with the current mode, switch nodes in
  any order.
- If transactions become incompatible, switch all receiving replicas,
  including anonymous ones, to `'old'` first, then switch the master.
  No `box.ctl.replicaset_sync()` call is needed for this compat switch.
- Switching in an arbitrary order while incompatible writes run can stop
  appliers. Switch the affected replicas to `'old'`, then manually restart
  their stopped appliers.

Returning to `'old'` does not restore a cleared limbo owner. If the legacy
workload needs sync writes and there is no owner, promote a leader before
those writes can succeed. Returning to `'old'` is unavailable in 5.x.

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
The `bootstrap_strategy = 'legacy'` won't probably be ever deleted from
Tarantool and we'll have to support code for it endlessly, don't like that.

The current behavior (`mixed`) doesn't work: it produces split brains if
`promote` is called with `election_mode = 'off'` or if sync and async spaces are
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
but the same boundary can be achieved without restart. The problem with restart
is that it can take hours and stop RO requests, we should avoid that.

### 3.4. Keep the `is_sync` for app compatibility

There was an idea not to break applications and allow `is_sync` partially. But
this means we won't be able to drop it at all and we'll always have these
checks in code, even when `is_sync` is not used at all anymore. It was decided
to break the app one time and call it a day. Moreover, it's easy to fix it:
just drop all `is_sync` in code and in migrations.

Deprecate per-space replication modes, but keep `is_sync` as a compatibility
no-op argument. Section 2.2 describes removing the argument completely.

Under `'new'`, `box.schema.space.create()` and `space:alter()` accept:

| Mode      | `is_sync = true` | `is_sync = false` |
| --------- | ---------------- | ----------------- |
| `'sync'`  | Accepted         | Accepted          |
| `'async'` | Error            | Accepted          |

In `sync` mode passing `is_sync = false` doesn't make a space async anymore,
it's always sync (as it is for `box_consider_system_spaces_synchronous`, where
the space is async in `_space`, but it's implicitly sync), but in 4.0 the flag
in `_space` is still written. In `'async'`, accepting `is_sync = true` would
silently weaken the requested guarantees, so it's prohibited and causes an
error.

`box.begin()`, `box.commit()`, and `box.atomic()` accept `is_sync = true` in
`sync` mode (`is_sync = false` is an error now and it'll remain an error in any
mode). Passing `is_sync = true` in `async` mode causes an error from now on, we
cannot fulfill these guarantees. The same applies to `IPROTO_IS_SYNC` in
`BEGIN` and `COMMIT`.

Under `'new'`, `box_txn_make_sync()` (which is public) is a complete no-op
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

If we want to drop the `is_sync` completely (as it's described in
section 2.2), a new compat will have to be introduced.

### 3.5. Allow `election_mode = 'off'` in synchronous mode

The previous proposal allowed registered sync replicas to use `'off'`. They
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

Checking modes only when connecting cannot detect incompatible transactions
after a dynamic mode change on an established connection. We may end up in the
situations, where one node has async replication mode, another one -- sync, and
on master switch the transaction behavior changes, split brain is possible.
It's possible to avoid that, so let's do that.

Section 2.3 therefore checks incoming records when the receiver uses `'new'`
and the originator is known to use `'new'`. This allows rolling upgrades with
mixed writes without changing `_space` or pausing the workload and still
protects clusters using only `'new'` from configuration errors.

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

### 3.8. Log a deprecation warning

We could log one warning per instance run under `'old'` and point users to
`replication_mode`. Warning whenever a space has `is_sync = true` would also
warn users whose replicated transactions are all synchronous. They already
avoid mixed replication, so the warning would be confusing.

A more useful warning would appear when an async replicated transaction is
received while the limbo has an owner. That detects actual mixed use, but
requires extra checks in the transaction hot path. Logging only once still
requires checking whether the warning has already been logged (`say_warn_once`).

So, it was decided to avoid adding that. Make users check the docs.

### 3.9. Expose a separate flush function

Exposing `txn_limbo_flush()` as `box.ctl.synchro_queue_flush()` would only
submit pending limbo entries to the journal, without waiting for their WAL
writes. Every procedure here would still need a following `wal_sync()` call,
so a separate public function adds little value. It's way too low-level for
export.

The non-rotating path of `txn_journal_flush()` already combines both waits.
Exposing it as `box.ctl.txn_wal_sync()` would give users a general way to wait
for prepared transactions to reach WAL, useful beyond these migrations
(proposed by @Gerold103). But the name does not explain how it differs from
`wal_sync()`.

`box.ctl.synchro_wal_sync()` is also unclear: ordinary `wal_sync()` already
waits for synchronous transactions' WAL writes once they reach the journal.
The extra work is including transactions still waiting for limbo space.

Instead, extend the existing operation with an option:

```lua
box.ctl.wal_sync({include_synchro_queue = true})
```

The operation remains WAL synchronization; the option expands which
transactions it covers. Its default is `false`, keeping existing behavior.
The name also matches `replicaset_sync({include_anon = true})`.

Other option names were considered:

- `flush_synchro_queue` is reasonable, but can suggest emptying the queue.
- `wait_synchro_queue` suggests waiting until the limbo is empty.
- `sync_synchro_queue` can suggest waiting for replication or quorum.

The description of `include_synchro_queue` is:

> Include prepared transactions waiting for synchronous queue space in the
> WAL synchronization. This does not wait for their confirmation.

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
