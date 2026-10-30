# Development of replication

This document explains more details about the replication system in Tarantool.
It gives more context when working specifically with replication code and its
logic.

## Overview

The replication code is primarily located in the `src/box/` library, in files:
- `applier.*`
- `relay.*`
- `txn_limbo*`
- `raft*`.

Instances in a replicaset are identified by 3 ids:
- UUID - unique and usually auto-generated.
- Instance name - optional, always provided manually.
- Numeric ID - a number between 1 and 31. It is assigned to new replicas by any
  writable instance in the replicaset, as the replicas join it.

In Tarantool the journal is not linear. Its position isn't by a single sequence
number, but instead by an array of sequence numbers: vclock.

Vclock is a vector where item with index `[ID]` stores LSN of the instance
having this numeric ID as its identifier.

Vclock size is hard-limited to 32, where the `[0]`-component is reserved for
so-called local transactions, which are never replicated.

This means that max number of identified replicas in a replicaset is 31.

Users can also connect so-called anonymous replicas. They do not get a numeric
ID or a name, they only have a UUID. Such replicas do not participate in
synchronous replication or any quorums in any way.

## Synchronous replication

While asynchronous replication is pretty simple and doesn't have much
complexity, the synchronous replication is the most complex part of this all.

It uses Raft protocol as its basis, and tries to implement it to the letter. But
historically there were some bumps on this road, because of which Tarantool has
some legacy logic which violates Raft.

Besides, vanilla Raft by design has a linear journal with single sequence number
on all replicas. Tarantool, OTOH, has a vector clock in its journal, which
originally was made this way to support asynchronous multi-master replication.

Also, vanilla Raft by design is able to roll back transactions right from the
end of its journal. In Tarantool it is not possible. What gets written into the
journal, can no longer be removed. And any sort of cancellation or rollback must
be done via another entry in the journal, while the transaction data for undoing
it must be kept in memory.

To make the cognitive load easier Tarantool splits Raft into election and
synchronous transaction processing.

The election is almost entirely vanilla, in `src/lib/raft/` is the base and in
`src/box/raft*` is a storage-aware wrapper.

The transaction processing had to be altered significantly due to the
fundamental differences of Tarantool journal and Raft's vanilla journal. It is
all located in `src/box/txn_limbo*` files.

The idea is embodied in the `txn_limbo` object. It is a queue of synchronous
transactions, and other transactions which depend on the synchronous ones. Here
the transactions are stored after their WAL write until they gain enough ACKs
and the leader writes `CONFIRM` entry for them, making them committed.

As `CONFIRM` reaches the replicas via the replication channels, they also apply
it, and commit these transactions from their copies of the limbo.

The biggest difference with Raft is a new entry called `PROMOTE`. It is what an
instance emits when it wins the Raft elections, and needs to persist this fact
in the journal.

Essentially, this `PROMOTE` entry in vanilla Raft terms could be called "the
first transaction in the new term". And it does the same job in Tarantool - it
commits pending transactions from the previous terms, if there are any.

---

If anything in this document seems outdated from how the code actually works,
then it must be immediately flagged to the user.
