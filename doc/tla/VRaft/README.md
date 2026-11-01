# Forward Vector Raft

Forward Vector Raft (VRaft) is a modification of the Raft consensus
protocol [1].

Its key differences are:
- Vector clock instead of a single position index.
- Separation of leader election and synchronous replication.
- Guaranteed split-brain detection in low-quorum cases.
- Compatible with append-only Write Ahead Logs.

It is used in Tarantool DBMS.

## 1. Setting

A **replica set** is a few nodes, each holding a copy of the **journal**: an
append-only log of rows - the write-ahead log (WAL) of a database, in which
nothing is ever removed or overwritten.

Every row has an **origin** (the node that wrote it) and an **LSN**, a log
sequence number from the origin's own monotonic persistent counter. The pair
identifies the row everywhere.

A node's journal contains its own rows and the rows it received from the others,
in the order it received them - its **WAL order**. Unlike a Raft log, whose
entries all come from the leader and are identified by index and term, the
journal interleaves rows of many origins and identifies them by origin and LSN.

Nodes replicate to each other over **links**. The link from A to B carries
A's journal to B: A sends the rows B lacks, in A's WAL order, and B sends
back **acknowledgements** - its **vclock**, a vector clock holding for every
origin the LSN up to which B has that origin's rows - over the same
connection. (In Tarantool the receiving end of a link is the *applier*, the
sending end the *relay*.)

// TODO: explain how the terms and votes are send out of order.

In a full mesh every node has a link from every other, so a row written by A
can reach B directly from A or relayed through a third node C; either way the
rows of one origin always arrive in LSN order, and a node relaying rows never
reorders them across origins relative to its own WAL.

Some nodes are **voters**: they take part in elections and nothing else -
they hold no data and acknowledge nothing. They only keep track of the terms and
their votes. The rest are **data nodes**.

Elections are Raft's [1, section 5.2], basically unchanged. Time is divided into
**terms**. A node starts an election by moving to a new term and voting for
itself, becoming a candidate. The others at that term vote once each, and only
for a candidate whose journal contains everything they have — Raft's election
restriction [1, section 5.4.1]. Where Raft compares the index and term of the
last log entry, VRaft compares vclocks, the candidate's at or above the
voter's in every component.

A candidate with a **quorum** of votes is the Raft leader of the term. A node
that sees a higher term moves to it and becomes a follower, dropping any
leadership. The terms travel both with the rows and with the acknowledgements,
so a node learns of a higher term over a link in either direction.

A leader leaves its leadership by seeing a higher term or by starting a new
election itself (a resigned or restarted leader is a follower of the same term
that can never be elected in it again, so for the protocol it is the same as a
leader that will start a new election).

One number, the **quorum**, serves both the elections and the confirmations for
"decisions" like transactions. When the quorum is a majority of the nodes, any
two quorums intersect - this is the ideal case, where the nodes never conflict
each other. When the quorum is 50% of the cluster or lower, this gets dangerous,
and the protocol is catching these cases.

## 2. The limbo

A transaction is **synchronous** when its commit waits for a quorum of nodes to
hold it. Synchronous transactions are not committed when written; they wait in
the **limbo** - the queue of written, not yet committed transactions - until a
quorum of nodes has acknowledged them.

The limbo has an **owner**, the node allowed to write synchronous transactions,
and the owner's **term**. Only the leader of the owner's term is a working
owner: the limbo is writable on a node exactly when the node owns it, is the
leader, and its election term equals the limbo's term. This is the separation
from the vanilla Raft: a leader can be elected, but it doesn't immediately own
the limbo. The leader needs to be promoted. The nodes, which didn't see the new
leader's promotion, still consider the limbo owned by its previous owner.

The owner writes a transaction, replicates it, counts the acknowledgements of
nodes that have it (its own write is one), and when they reach the quorum writes
a **CONFIRM** row: *"my rows up to LSN X are confirmed"*. Every node applying
that CONFIRM commits the owner's transactions up to X.

Nothing is ever committed without a CONFIRM row for it; a transaction never
confirmed by its own author can be rolled back or confirmed by the next leader.

In the vanilla Raft the commit point is the leader's commit index, carried in
messages and never in the log; here it is a row of the journal, replicated,
relayed and recovered like every other row, which is what lets a node's state be
reconstructed from its journal alone.

An acknowledgement carries the election term of the node sending it, at the time
of sending. The owner processes the term first: an acknowledgement from a node
that moved to a higher term is not counted, it **demotes** the owner - the owner
moves to the higher term and is a follower, and the limbo is not writable on it
anymore.

An acknowledgement may be delivered late - after its sender moved on - and still
counts, because its term was valid when sent.

Acknowledgements travel back over the link they acknowledge, the one from the
owner to the acknowledging node; a node that closed that link is never heard
from again by the owner, whatever it receives through others.

Every node derives from the rows it has applied a **confirmed vclock** - for
each origin, the LSN up to which that origin's rows are confirmed - and an
**applied term map** - for each node, the term of its latest promotion that was
confirmed. Both are what the checks against split-brain compare against.

## 3. Changing the owner: PROMOTE

When a node wins an election it does not own the limbo yet. It writes a
**PROMOTE** row claiming it:
```
PROMOTE {
  origin,
  lsn,
  term,
  prev_owner,
  confirmed_vclock,
  term_map,
}
```

- `origin` - the ID of the node which made this promote.
- `lsn` - LSN of the promote's journal entry in the origin's journal.
- `term` - the election term in which the origin node won the elections,
  and in scope of which it is trying to claim the limbo.
- `prev_owner` - previous owner, the ID of the limbo owner which the origin saw
  when made this promote.
- `confirmed_vclock` - the origin's vision of the latest confirmed LSNs of all
  the nodes in the cluster, which ever were leaders.
- `term_map` - the origin's vision of latest of the latest terms of confirmed
  promotions of all nodes in the cluster, which ever were leaders.

The previous owner ID and the confirmed vclock together say the verdict on the
previous owner's queue - its transactions up to `confirmed_vclock[prev_owner]`
LSN are to be committed, the ones after it rolled back.

The new leader decides this from what it holds: everything it has of the
previous owner's queue is to be committed, since the election guarantees that
whatever could have gathered a quorum anywhere is in the new leader's journal.

The PROMOTE is VRaft's counterpart of the entry a new Raft leader commits
at the start of its term to settle the fate of the previous terms' entries
[1, sections 5.4.2 and 8].

However two things differ. The journal is append-only, so the previous owner's
unconfirmed transactions are not truncated the way Raft truncates a conflicting
log suffix: they stay in the journal, and the verdict - commit up to the confirm
LSN, roll back the rest - is a decision recorded in the PROMOTE and carried out
by every node when the PROMOTE takes effect.

In order for this to work the PROMOTE can not take effect when written. It
remains **pending** - on its author and on every node that receives it - until
its author has collected a quorum of acknowledgements for it, exactly like a
transaction, and writes the PROMOTE's own **CONFIRM**: *"my rows up to the
PROMOTE's LSN are confirmed"*, the same row kind as a transaction's CONFIRM,
naming the PROMOTE's own row.

Essentially, PROMOTE can be considered the limbo owner transition transaction,
and follows the same rules are regular transactions do.

Only the author confirms its PROMOTE, and only while it is still the leader of
that term: a leader that lost the term cannot confirm, and its pending PROMOTE
stays where it is, like on every other node, until a newer promotion supersedes
it.

Applying a PROMOTE's CONFIRM, on any node, is the change of power:
- the PROMOTE's author becomes the owner of the limbo.
- the PROMOTE's term is assigned to the limbo.
- the previous owner's queued transactions are all committed or rolled back,
  depending on their position against the confirmed LSN.
- the limbo's confirmed vclock and term map are replaced by the PROMOTE's;
- pending promotions whose origin's election term is now at or below the applied
  map are dropped as **covered** (chained from the just applied PROMOTE, see
  more on that below).

From then on the node accepts transactions from the new owner and no one else.

Each node keeps **one pending PROMOTE per origin**, the latest - the origin's
**slot**. A newer PROMOTE from the same origin replaces an older pending one.
This is legit, because if we got one PROMOTE after another from the same node,
it means the first one won't be confirmed anymore. This is guaranteed by the
replication ordering - we receive a node's rows always strictly in the same
order as the node wrote them.

Pending promotions are kept even when they can no longer be applied: their
CONFIRM may still arrive and must be recognized for the sake of split brain
detection.

## 4. Poisoned promotions and chaining

A pending PROMOTE is **poisoned** on a node when no CONFIRM can ever apply
it there:

- its term is at or below the node's applied limbo term - a newer
  promotion was confirmed meanwhile, this one is superseded and wasn't included
  into the newer term's promotion;
- its term map is behind the node's applied map in any component - its
  author did not know of a promotion this node has confirmed. Under a
  majority quorum this cannot happen; with a smaller one it is the signature of
  a split-brain;
- its confirmed vclock is behind the node's - it would confirm less than is
  already confirmed here, the same signature.

Poison is a property of the pair (node, PROMOTE): the same PROMOTE can be
live on one node and poisoned on another. A poisoned PROMOTE stays in its
slot; its CONFIRM, if it comes, is refused.

A leader whose own pending PROMOTE got poisoned gives up the promotion:
- poisoned by a newer promotion means the elections moved on;
- poisoned by an older one confirmed behind its back means a split brain.

It is similar to how the original Raft keeps the commit index of all nodes
monotonic. Except that Raft doesn't have a concept of a split-brain and hence
needs no detection of it.

**Chaining.** A newly elected leader may hold a pending PROMOTE of someone
else - or its own, from a previous attempt - that is still live. It cannot
know whether that PROMOTE will yet be confirmed by its author. So its own
PROMOTE is **chained** on top. It is like adding a new pending transaction
after an already existing and also pending one.

Except that the chain isn't actually stored as a list. These pending promotions
need to be kept in-memory, for validation and newer chaining, and then their
confirmation. Storing the pending promotions as a list combined with potentially
unlimited term bumps would produce an infinitely growing in-memory list.

But it is actually possible to express a chain of promotions of any length as a
single promotion. Because what one promotion wants to modify is all stored in
the promotion itself: the limbo term, confirmed vclock, term map. One promote
chained on top of another simply means taking the existing pending promote,
propagating its limbo term and the needed components of the confirmed vclock
and of the term map. The resulting single promote, when applied, brings the
limbo to the same state as applying the 2 original promotions one by one.

This allows to limit the max number of pending promotions in memory to the
number of nodes in the replicaset. Each pending promotion might express effects
of any number of older "chained" promotions, which simply all got squashed into
one.

When a new pending promote is made, the previous pending one is kept in its
slot as pending (if it wasn't from the same node). It might happen that this
older pending promote still receives a CONFIRM of its own.

Whichever of the two CONFIRMs (for the newest squashed chained promote, or for
the previous one in the chain) applies first on a node, and whether the other
ever does, the node ends in the same state; and once the newer PROMOTE is
confirmed the older one is detected as covered and dropped. A pending promotion
is covered by a newer one confirmed, when the newer one in its term map has the
older one's term or above. This means it was chained over it at some point.

A poisoned pending PROMOTE is never chained from. It stays in-memory as a guard.
Chaining from it would simply make the newer PROMOTE inherit the poison. 

Note than on chaining the newer PROMOTE inherits and propagates the older one's
fields, but the previous queue owner ID it inherits as is. Even if locally the
queue by now has a different confirmed owner.

A leader does not promote while it holds a pending PROMOTE of a term at or
above its own: the elections have moved past it and it does not know yet. This
can be possible because VRaft sends instant in-memory term updates via a
different channel than the normal replication. And thus a node might receive a
PROMOTE with a term above its local election term.

## 5. Transactions under a change of owner

A transaction row names its origin and LSN. Its term can be stored in it too,
but in practice it is not necessary. The transaction's term can be derived from
the last confirmed PROMOTE of the transaction's origin. The limbo tracks
exactly that in the term map.

Because rows of one origin arrive in order, the transaction is always received
after the CONFIRM of the PROMOTE that made its origin the owner, and before any
later row of that origin, so the derivation is exact.

A transaction from the current owner is queued, waiting for its CONFIRM. A
transaction from a node whose applied term is below the limbo's - an old
owner that kept writing - is rolled back on receipt and written down into the
journal as a nop, to keep the vclock component of this origin progressing.

A transaction from a node that is neither cannot arrive: its author would first
have had to promote itself and own the queue.

## 6. Refusals: split-brain detection

A **split brain** is two or more nodes working as owners and able to
simultaneously confirm new transactions.

This is only possible when quorums of such owners didn't intersect during
their promotions.

A **fork** is what it does to the data: a transaction committed on one side and
rolled back on the other.

Nodes detect both by their rows. A node refuses a row that contradicts its
state, and a refusal **closes the link** the row came over, permanently: the
receiving end stops with a split-brain error and nothing from that source is
received again.

The refused row is not journaled - the row is poison for the limbo. It would
contradict the previously confirmed data stored on this node. This also means
the vclock component of the rejected node on the received node will be frozen,
not progressing at or beyond the conflicting row.

The refusals all boil down to these:

- a **PROMOTE** of a term the node knows as taken by another node - in its
  applied map, or as another node's pending PROMOTE. A term hosts at most
  one PROMOTE; a second one is a second leader of the same term. This detection
  works on the PROMOTE row itself only when this exact term is already known to
  have a pending or confirmed PROMOTE. If this PROMOTE's term is < than the
  latest confirmed one, then it means nothing yet. This row is accepted and
  becomes pending with poison.

- the **CONFIRM of a PROMOTE** that is poisoned here - its author confirmed
  it without knowing something this node has confirmed, or confirms less
  than is confirmed here. Either way some other node made a decision without
  intersecting with the quorum, which the receiving node participated in.

- the **CONFIRM** from an older term, which is trying to confirm something new,
  to propagate the confirmed vclock. It means some rogue older leader remains
  active and even tries to commit, clearly having a quorum non-intersecting with
  the one of the receiving node.

A CONFIRM whose LSN is at or below what the node already has confirmed for
that origin is nothing to do and is applied as a no-op: the late CONFIRM of
a PROMOTE already covered by a chain, for instance. Logically this doesn't
contradict the local state.

Nothing heals a closed link; an operator needs to find out why the link was
closed, where was the quorum misconfigured, and then the faulty node needs to be
rebuilt.

## 7. Guarantees under a majority quorum

With the quorum a majority, the protocol never refuses anything and the
following hold on every node at every moment:

- **One leader per term**. By the elections: a node votes once per term.

- **A confirmed promotion is known to every later leader.** Its quorum of
  acknowledgers intersects every later election's quorum of voters; a voter
  votes only for a candidate holding everything the voter holds; and an
  acknowledgement from a node that already moved to the later term is not
  counted. So a later leader's PROMOTE always carries the earlier one in its
  term map - **no regression**, no poison by an older promotion, and the
  "confirms less than confirmed" refusal never fires.

- **A confirmed transaction stays confirmed**: the new leader's verdict
  always covers it, by the same intersection.

- **Nothing is confirmed behind the leader's back**: a leader's own pending
  PROMOTE can be poisoned only by a newer promotion - the stale-leader case
  below - never by an older one.

Three non-trivial races are allowed under a majority and are safely handled,
still keeping all the guarantees. Worth listing them:

- **A PROMOTE ahead of its term.** In VRaft the election term is allowed to
  travel in its own message out of the replication order, and can be overtaken
  by the PROMOTE row when relayed through a third node. Thus a node may receive
  a pending PROMOTE of a term above its own election term. This is why a leader
  with a pending PROMOTE at or above its term does not promote. It already
  treats its current leadership lost.

- **A delayed acknowledgement.** A node acknowledges a PROMOTE, then votes a
  new leader in; its acknowledgement, carrying the old term, is still valid
  when it lands, and the old leader confirms its PROMOTE with a newer one
  already pending beside it. This is fine, because the newer PROMOTE saw the
  older one, and has it in the chain - when it gets confirmed, it won't
  contradict the older one, and vice versa.

- **A limbo leader with a newer PROMOTE pending.** The consequence of the
  previous two: a working currently confirmed fully functioning owner can hold a
  live PROMOTE of a higher term. The pending PROMOTE demotes nothing; the owner
  keeps writing until it sees the election term or a the CONFIRM for this new
  PROMOTE, and whatever it wrote in between is rolled back by the newer
  PROMOTE's verdict, consistently everywhere.

## 8. Guarantees under a smaller quorum

With the quorum at or below half, two leaders can be elected in one term, two
PROMOTEs can be confirmed by disjoint quorums, a transaction can be committed on
one side and rolled back on the other. The protocol cannot prevent this in
reality, where quorum can be a manual setting, or the cluster topology might be
written wrong in a config. But preventing it is not necessary, if any problems
coming out of such misconfigurations and bugs can be detected.

The promise is that **a fork is detected on the first row that reveals it.** A
conflicting row is always refused: the second PROMOTE of a term, the CONFIRM of
a promotion made without knowing another, the CONFIRM of a transaction nobody
queued.

Refused rows are not journaled, which means **a node that holds every row
another node holds agrees with it**: it has committed everything the other
committed, in the same order, rolled back everything the other rolled back, and
confirmed every promotion the other confirmed. In particular, **two nodes
holding the same rows have the same state**, whatever order the rows came in.

This same as the vanilla Raft guarantee - when quorum is a majority, any 2 nodes
up to their committed log indexes either have exactly the state, or one is the
prefix of the other (look exactly like the other some time ago, and will catch
up).

VRaft promises the conflict detection, but it is not always two-sided. Sometimes
one node will break the link from its peer, but the peer might not yet
immediately break its link to the first node.

For example:
- Mutual break: two leaders of one term will mutually refuse each other's
  PROMOTEs, only while their own one for that term is pending, or is confirmed
  but not overridden by a newer term yet.
- One-sided break: a leader re-elected before its PROMOTE was confirmed chains
  over it and forgets the old term; it then stores the other leader's PROMOTE as
  live pending, and learns of the fork only when that leader's CONFIRM arrives
  and poisons its own pending by regression - or never, if that CONFIRM does not
  come, and then it simply follows.

This essentially means, that if some not yet committed but potentially
conflicting transaction comes (an actual data transaction, or a not yet
confirmed PROMOTE), then it is let in - this can happen in majority quorums too.

The conflict is guaranteed to be detected only when the conflict state attempts
to get confirmed: a CONFIRM for a noped transaction or for a poisoned PROMOTE.

This guarantee still is enough to rely on never having 2 nodes forked and still
running the replication, propagating the fork even further. An error will break
at least one link immediately, and the second link very likely, attracting
attention from an administrator.

## 9. The model

The protocol above is written as a state machine in TLA+ [2], in `VRaft.tla`,
and checked by TLC, the TLA+ model checker, which enumerates every reachable
state of a finite instance of it.

Essentially TCL makes a fullscan of all possible states of an evolution of a
certain system, like a replicaset with a specific config. Thousands, millions,
even billions of states are checked to make sure that no matter how the rows
are replicated at which times, what configs are set, how many nodes are in the
cluster - still the system remains consistent, with no undetected forks or
split brains.

One family of scenarios are the ones where a system is described, like N nodes,
Q config, T transactions to make, and then its evolution is validated through
all possible states. These are described in `test.yaml`.

Another family of scenarios are the ones looking for a specific sequence of
actions to happen, leading to an expected outcome. Like unit tests. For them
TLC checks all the states until the needed actions happen with the right
outcome. This allows to check the model isn't missing or excluding any specially
interesting cases, before it runs fullscan on the large billion-states configs.
These are described in `test_witness.yaml`.

Running a close-to-real system wouldn't be realistic though. The state space
quickly grows to tens of billions and more as the model and configs take more
and more details into account, which can make the validation run for weeks,
months, requiring terrabytes of disk space.

This usually means that the model must simplify away some irrelevant details.
Detailed description of the model assumptions can be seen in the model source
code, but here are a few examples:

- In the model the limbo holds maximum one transaction at a time. This is fine,
  because it scales to multiple transaction by induction, but allows to reduce
  the state space drastically.

- Nodes can be 'voters' - such nodes receive no rows, but keep track of terms
  and can vote. This allows to test scenarios with more nodes in the replicaset,
  while keeping the election protocol valid and not exploding the state space
  by journals of the voters.

The guarantees of VRaft are verified via assertions and invariants.

See more in the source code of the model.

## 10. Running the validation

Configs come from the two matrices through the generator; the runner runs
them in order from fastest to slowest and stops at the first unexpected outcome.

It is important to note that the duration of the longest scenarios can be up to
non-stop a few days on average hardware. But the shortest scenarios take from
seconds to hours to run even on a laptop.

It is recommended to have a look at `python3 tools/run.py --help` to see the
available options before running any of the below. It is important to set the
right settings corresponding to the target hardware, to make sure the tests
run not too long, don't OOM, and don't get stuck.

```
# Generate the TLA+ specs for the test scenarios matrix.
python3 VRaft/gen.py VRaft/test.yaml var/test
python3 VRaft/gen.py VRaft/test_witness.yaml var/witness

# Validate the reachability of some states in various cluster setups.
# These tests will run up to a few minutes.
python3 tools/run.py --tlc /path/to/tla2tools.jar --witness \
    --metadir var/witness/states --spec VRaft/VRaftWitness.tla var/witness/*.cfg

# Validate all states of various cluster configurations.
python3 tools/run.py --tlc /path/to/tla2tools.jar --java "java -XX:+UseParallelGC" \
    --java-heap 48g --fpmem 0.5 --checkpoint 30 --workers auto --interval 60 \
    --metadir var/test/states --spec VRaft/VRaft.tla var/test/*.cfg
```

For the purpose of debug, when changing the model and trying various new
scenarios, there is a tool for pretty-printing the TLC's state traces, which
can be quite hard to read otherwise: `tools/tlatrace.py`. It can be run directly
on the output of the TLC's java binary where the raw trace is printed. Usually
this means the output should be captured into a file, and the file then can be
fed to the tracer like so: `tools/tlatrace.py <out.txt> actions|diff|show N`.

## References

1. Diego Ongaro and John Ousterhout. *In Search of an Understandable
   Consensus Algorithm (Extended Version)*. Stanford University, 2014.
   Sections cited: 5.2 (leader election), 5.4.1 (election restriction),
   5.4.2 (committing entries from previous terms), 8 (the no-op entry at
   the start of a term).
2. Leslie Lamport. *Specifying Systems: The TLA+ Language and Tools for
   Hardware and Software Engineers*. Addison-Wesley, 2002. The TLA+ tools,
   including the TLC model checker: https://github.com/tlaplus/tlaplus.
