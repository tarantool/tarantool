# Forward Raft

Forward Raft is a modification of the Raft consensus protocol [1].

Its key differences are:
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
last log entry, Forward Raft compares vclocks, the candidate's at or above the
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

The PROMOTE is Forward Raft's counterpart of the entry a new Raft leader commits
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
and follows same rules are regular transactions do.

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
Pending promotions are kept even when they can no longer be applied: their
CONFIRM may still arrive and must be recognized for the sake of split brain
detection.









//// TODO














## 4. Poisoned promotions and chaining

A pending PROMOTE is **poisoned** on a node when no CONFIRM can ever apply
it there:

- its term is at or below the node's applied limbo term - a newer
  promotion was confirmed meanwhile, this one is superseded;
- its term map is behind the node's applied map in any component - its
  author did not know of a promotion this node has confirmed. Under a
  majority quorum this cannot happen (section 7); with a smaller one it is
  the signature of a fork;
- its confirmed vclock is behind the node's - it would confirm less than is
  already confirmed here, the same signature.

Poison is a property of the pair (node, PROMOTE): the same PROMOTE can be
live on one node and poisoned on another. A poisoned PROMOTE stays in its
slot; its CONFIRM, if it comes, is refused (section 6). A leader whose own
pending PROMOTE got poisoned gives up the promotion: poisoned by a newer
promotion means the elections moved on ("interfering elections"), poisoned
by an older one confirmed behind its back means a split brain.

**Chaining.** A newly elected leader may hold a pending PROMOTE of someone
else - or its own, from a previous attempt - that is still live. It cannot
know whether that PROMOTE will yet be confirmed by its author. So its own
PROMOTE is **chained** on top: it takes the pending one's previous owner,
confirm LSN, confirmed vclock and term map, and adds only its own term and
its own row. Whichever of the two CONFIRMs applies first on a node, and
whether the other ever does, the node ends in the same state; and once the
newer PROMOTE is confirmed the older one is covered and dropped. A poisoned
pending PROMOTE is never chained from - there is nothing to stay consistent
with, chaining would only inherit the poison. The base of a chain is the
only PROMOTE in it that writes the verdict on the previous owner; the ones
chained on top change nothing about it.

A leader does not promote while it holds a pending PROMOTE of a term at or
above its own: the elections have moved past it and it does not know yet
(the term may still be on its way, section 7). Re-election of the same node
over its own unconfirmed PROMOTE forgets the old PROMOTE's term - the slot
holds one per origin - with a consequence described in section 8.

## 5. Transactions under a change of owner

A transaction row names its origin and LSN, not its term (a Raft entry
carries its term; here only a PROMOTE does); a receiver derives the term
from its applied map: the term of the origin's latest confirmed promotion.
Because rows of one origin arrive in order, the transaction is always
received after the CONFIRM of the PROMOTE that made its origin the owner,
and before any later row of that origin, so the derivation is exact.

A transaction from the current owner is queued, waiting for its CONFIRM. A
transaction from a node whose applied term is below the limbo's - an old
owner that kept writing - is rolled back on receipt. A transaction from a
node that is neither cannot arrive: its author would first have had to
promote itself, and that PROMOTE is what gets refused.

## 6. Refusals: split-brain detection

A **split brain** is two nodes both working as the owner, each confirmed by
a quorum of its own - possible only when quorums need not intersect
(section 8). A **fork** is what it does to the data: a transaction committed
on one side and rolled back on the other, two promotions confirmed by
disjoint quorums. Nodes detect both by their rows. A node refuses a row that
contradicts its state, and a refusal **closes the link** the row came over,
permanently: the receiving end stops with a split-brain error and nothing
from that source is received again. The refused row is not journaled - so a
node's journal never contains two rows that contradict each other, and the
vclocks never advance past a refusal. The refusals:

- a **PROMOTE** of a term the node knows as taken by another node - in its
  applied map, or as another node's pending PROMOTE. A term hosts at most
  one PROMOTE; a second one is a second leader of the same term;
- the **CONFIRM of a PROMOTE** that is poisoned here - its author confirmed
  it without knowing something this node has confirmed, or confirms less
  than is confirmed here;
- a **CONFIRM** that applies to nothing queued and confirms nothing already
  confirmed - an old owner still confirming after its succession, or a
  confirmation of what was never received.

A CONFIRM whose LSN is at or below what the node already has confirmed for
that origin is nothing to do and is applied as a no-op: the late CONFIRM of
a PROMOTE already covered by a chain, for instance.

Nothing heals a closed link; an operator rebuilds the node from a copy of a
node on the other side, discarding its journal (in Tarantool, a
rebootstrap).

## 7. Guarantees under a majority quorum

With the quorum a majority, the protocol never refuses anything and the
following hold on every node at every moment:

- **One leader per term**, by the elections: a node votes once per term.
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

Three races are real under a majority and the protocol admits them:

- **A PROMOTE ahead of its term.** The Raft term travels in its own message
  and can be overtaken by the PROMOTE row when relayed through a third node.
  A node may hold a pending PROMOTE of a term above its own Raft term. This
  is why a leader with a pending PROMOTE at or above its term does not
  promote, and why nothing assumes the opposite.
- **A delayed acknowledgement.** A node acknowledges a PROMOTE, then votes a
  new leader in; its acknowledgement, carrying the old term, is still valid
  when it lands, and the old leader confirms its PROMOTE with a newer one
  already pending beside it.
- **A limbo leader with a newer PROMOTE pending.** The consequence of the
  previous two: a working owner holds a live PROMOTE of a higher term. The
  pending PROMOTE demotes nothing; the owner keeps writing until it sees the
  term or applies the CONFIRM, and whatever it wrote in between is rolled
  back by the newer PROMOTE's verdict, consistently everywhere.

## 8. Guarantees under a smaller quorum

With the quorum at or below half, two leaders can be elected in one term,
two PROMOTEs can be confirmed by disjoint quorums, a transaction can be
committed on one side and rolled back on the other. The protocol cannot
prevent this and does not try. What it promises instead:

- **A fork is detected on the first row that reveals it.** A conflicting row
  is always refused: the second PROMOTE of a term, the CONFIRM of a
  promotion made without knowing another, the CONFIRM of a transaction
  nobody queued. Refused rows are not journaled, so:
- **A node that holds every row another node holds agrees with it**: it has
  committed everything the other committed, in the same order, rolled back
  everything the other rolled back, and confirmed every promotion the other
  confirmed. In particular, **two nodes holding the same rows have the same
  state**, whatever order the rows came in.

Detection is not always two-sided. Two leaders of one term refuse each
other's PROMOTE only while their own one for that term is pending. A leader
re-elected before its PROMOTE was confirmed chains over it and forgets the
old term; it then stores the other leader's PROMOTE as live, and learns of
the fork only when that leader's CONFIRM arrives and poisons its own
pending by regression - or never, if that CONFIRM does not come, and then it
simply follows. It confirmed nothing of its own in between, so the one-way
link breaks nothing: the follower holds everything the other side holds.

## 9. The model

The protocol above is written as a state machine in TLA+ [2], in
`ForwardRaft.tla`, and checked by TLC, the TLA+ model checker, which
enumerates every reachable state of a finite instance of it; the
reachability witnesses are in `ForwardRaftWitness.tla`. Where the model
simplifies the protocol it says so in a comment: the owner has one
transaction in flight at a time (the all-or-nothing verdict; a partially
covered queue is not modeled); voters receive no rows at all (so a quorum
above the data node count can never be met, and such configurations are not
generated); explicit rollback and resignation rows (Tarantool's ROLLBACK and
DEMOTE) and link recovery are out of scope; a run is bounded by a maximum
term and optionally by a maximum number of PROMOTEs in the cluster. Where a
choice mirrors a particular piece of the Tarantool implementation, the
specification's comment names it.

What is checked, as invariants on every state:

- the guarantees of section 8 - a node holding another's rows covers its
  decisions and its confirmed promotions; same rows, same state;
- the limbo's writable state equals its derivation (owner, Raft leader,
  terms equal), both directions;
- the applied term map against the limbo term: nothing above it, the
  owner's term equal to it, no term applied for two origins.

The guarantees of section 7 are asserts inside the actions, active only when
the quorum is a majority: no refusal ever, one PROMOTE per term, nothing
confirmed behind the leader's back. Facts the protocol relies on are
asserted too, so that TLC proves them: a PROMOTE is never covered on
arrival, a non-owner's transaction is always from an old term, the only
CONFIRM an origin with a pending PROMOTE can send is that PROMOTE's.

The **witnesses** are the negation of scenarios the model must reach -
sections 7 and 8 in executable form: two leaders of one term, a PROMOTE
ahead of its term, a transaction committed here and rolled back there, the
CONFIRM of a poisoned PROMOTE refused, a chain confirmed, an own PROMOTE
confirmed with a newer one pending, a CONFIRM carried by a delayed
acknowledgement, a limbo leader with a newer PROMOTE pending, the cluster
split into two consistent groups, every link broken; and the
three-promotion patterns a fourth term is for - a chain through a chain, a
chain revisiting its origin and squashing the origin's own pending slot, a
poisoned PROMOTE replaced by its origin's later one, three fork sides - run
with the PROMOTE cap the big config runs with, so they double as the proof
that the cap keeps them reachable. TLC violating a witness is the goal; its
trace is the scenario. A witness that passes means the model is too strict -
a change broke a behavior the protocol has.

## 10. Running the checks

Configs come from the two matrices through the generator; the runner runs
them in order and stops at the first unexpected outcome.

```
python3 ForwardRaft/gen.py ForwardRaft/test.yaml var/test
python3 ForwardRaft/gen.py ForwardRaft/test_witness.yaml var/witness

python3 tools/run.py --tlc /path/to/tla2tools.jar --witness \
    --metadir var/witness/states --spec ForwardRaft/ForwardRaftWitness.tla var/witness/*.cfg

python3 tools/run.py --tlc /path/to/tla2tools.jar --java "java -XX:+UseParallelGC" \
    --java-heap 48g --fpmem 0.5 --checkpoint 30 --workers auto --interval 60 \
    --metadir var/test/states --spec ForwardRaft/ForwardRaft.tla var/test/*.cfg
```

`test.yaml` entries have `data_nodes`, `voters`, `quorum`, `max_term`,
`transactions`, optionally `max_promotes` (a cap on the PROMOTEs the whole
cluster writes; the natural maximum is `data nodes * (max_term - 1)`, the
default 999 never binds), `invariant` or `property` (an extra check), and
`simulate` with `traces: N` and `depth: D` (the config is run by TLC's
random simulation, below, instead of the exhaustive search). The names
carry their position, so a shell glob runs them in matrix order - ascending
by measured duration: 01-04 take seconds, 05 and 06 about 15 and 30 minutes
on 16 cores, 07 hours, 08 22 hours (with one transaction and four PROMOTEs;
with two transactions and six PROMOTEs it passed 3.4 billion states in two
days at depth 29 of about 50 and was abandoned); 09 and 10 are simulated, a
billion traces each - days on 16 cores at the measured rate, to be timed on
the server. **Do not start 05-10 casually.**
The witnesses take 20 seconds together and are the first thing to run after
any change to the spec. `run.py --help` explains every flag; the heap and
fingerprint flags matter on the big runs (a starved JVM deadlocks TLC
silently), `--metadir` is required and is cleaned after an expected outcome,
kept after a failure with the checkpoint to `-recover` from. Each run's full
TLC output lands next to its config as `<name>_out.txt`;
`tools/tlatrace.py <out.txt> actions|diff|show N` reads a trace.

A `simulate` entry replaces the exhaustive search with TLC's random
simulation: N behaviors of up to D steps from the initial state, checked
along the way, nothing remembered between them, until a violation or the
N-th behavior. A violation found this way is real and comes with its trace;
a clean end proves nothing, N only bounds the run. gen.py writes the choice
into the config's header as `\* run: {"simulate": {...}}` and run.py reads
it there, so one invocation runs the mixed matrix. The command line
`--simulate [--depth D] [--traces N]` forces simulation on every config
given, for ad-hoc runs; without `--traces` it runs until a violation or a
kill. Hours of such a run on 08's shape found the third poison (section 4),
which no exhaustive config of a feasible size reaches; it then reproduced
on `04`.

For a change to the model, compare "distinct states found" and "states
generated" before and after on 01-04 or on a custom small config: a
performance change must leave the distinct count identical; a change that
removes behaviors must say which and why the protocol does not have them.
One TLC rule worth knowing: an `\E` used as a guard inside an action is not
a boolean, TLC splits the action into one successor per witness - a guard
must be a `~\A`, a `Cardinality` or a set comparison. Parse with
`java -cp tla2tools.jar tla2sany.SANY ForwardRaftWitness.tla` from this
directory.

## References

1. Diego Ongaro and John Ousterhout. *In Search of an Understandable
   Consensus Algorithm (Extended Version)*. Stanford University, 2014.
   Sections cited: 5.2 (leader election), 5.4.1 (election restriction),
   5.4.2 (committing entries from previous terms), 8 (the no-op entry at
   the start of a term).
2. Leslie Lamport. *Specifying Systems: The TLA+ Language and Tools for
   Hardware and Software Engineers*. Addison-Wesley, 2002. The TLA+ tools,
   including the TLC model checker: https://github.com/tlaplus/tlaplus.
