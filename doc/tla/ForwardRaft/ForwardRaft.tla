----------------------------- MODULE ForwardRaft -------------------------------

EXTENDS TLC, Integers, Sequences, FiniteSets

--------------------------------------------------------------------------------
\*
\* Constants
\*

\* Node IDs in the cluster
CONSTANT NodeIDs
CONSTANT VoterIDs

\* All transactions to execute (set of identifiers)
CONSTANT AllTransactions

\* Maximum term allowed before stopping
CONSTANT MaxTerm

\* The PROMOTEs the whole cluster may write. The natural maximum is one per
\* node per electable term, data nodes * (MaxTerm - 1); anything at or above
\* it never binds. A lower value cuts the breadth of the big runs - not every
\* node promotes in every term - and keeps the depth. The witnesses are the
\* check that the scenarios survive it.
CONSTANT MaxPromotes

\* Initial limbo owner (must be in NodeIDs)
CONSTANT InitialLimboOwner

\* Election and ack quorum. A majority makes any two quorums intersect. A
\* smaller one lets two leaders get elected by disjoint supporters, even in
\* the same term - a fork the protocol can't prevent, and must detect.
CONSTANT Quorum

\* Null value
CONSTANT NULL

\* Entry types
CONSTANT EntryTypeTransaction
CONSTANT EntryTypePromote
CONSTANT EntryTypeConfirm

\* Raft states. A candidate is a node collecting the votes of the term it
\* started; only a candidate counts them. A leader leaving its leadership on
\* its own - fenced, demoted, restarted - is in Tarantool a follower of the
\* same term with its vote still cast, never elected again in that term: it
\* can only observe a higher term or start a new election. Both are available
\* to the leader here directly, so that follower is not a state of its own.
CONSTANT RaftStateFollower
CONSTANT RaftStateCandidate
CONSTANT RaftStateLeader

\* Limbo states
CONSTANT LimboStateReplica
CONSTANT LimboStateLeader

CONSTANT NodeRoleCandidate
CONSTANT NodeRoleVoter

\* Maximum expected journal length per node
MaxJournalLength == (MaxTerm * 2 + Cardinality(AllTransactions) * 2) * Cardinality(NodeIDs)

\* The nodes receiving replication.
DataNodes == NodeIDs \ VoterIDs

\* Symmetry - the data nodes are equivalent among themselves, so are the
\* voters and the transactions. Sound only because the role is in the state:
\* a permutation across the roles maps every reachable state to an
\* unreachable one, so the role-mixing permutations would merge nothing, and
\* only cost - every permutation is applied to every state.
Perms == Permutations(DataNodes) \union Permutations(VoterIDs)
         \union Permutations(AllTransactions)

\* Any two majorities intersect, so the elections and the acks make forks
\* impossible. The checks holding only then are asserted under this guard.
IsMajorityQuorum == 2 * Quorum > Cardinality(NodeIDs)

--------------------------------------------------------------------------------
\*
\* Variables
\*

\* Per-node state
VARIABLE Nodes

\* Remaining transactions to create (set)
VARIABLE TransactionsToDo

\* The PROMOTEs written so far, all nodes together. A function of the
\* journals, so no state of its own - the guard on it is what matters.
VARIABLE PromotesWritten

vars == <<Nodes, TransactionsToDo, PromotesWritten>>

--------------------------------------------------------------------------------
\*
\* Helper functions for object field manipulation
\*

\* Single field setters - operate on node objects directly
SetRaftTerm(v, s) == [s EXCEPT !.raft_term = v]
SetRaftState(v, s) == [s EXCEPT !.raft_state = v]
SetRaftVote(v, s) == [s EXCEPT !.raft_vote = v]
SetLimboState(v, s) == [s EXCEPT !.limbo_state = v]
SetLimboTerm(v, s) == [s EXCEPT !.limbo_term = v]
SetLimboOwner(v, s) == [s EXCEPT !.limbo_owner = v]
SetLimboVclock(v, s) == [s EXCEPT !.limbo_vclock = v]
SetLimboTermMap(v, s) == [s EXCEPT !.limbo_term_map = v]
SetLimboPromotions(v, s) == [s EXCEPT !.limbo_promotions = v]
SetLimbo(v, s) == [s EXCEPT !.limbo = v]
SetData(v, s) == [s EXCEPT !.data = v]
SetDataRejected(v, s) == [s EXCEPT !.data_rejected = v]
SetJournal(v, s) == [s EXCEPT !.journal = v]
SetJournalVclock(v, s) == [s EXCEPT !.journal_vclock = v]
SetValidAcks(v, s) == [s EXCEPT !.valid_acks = v]
SetMadeTxn(v, s) == [s EXCEPT !.made_txn = v]
SetAppliers(v, s) == [s EXCEPT !.appliers = v]

\* Array operations
ArrAppend(v, s) == Append(s, v)
ArrContains(v, s) == \E i \in DOMAIN(s): s[i] = v

\* Update a node in the Nodes dictionary
NodesUpdate(nid, node) == [Nodes EXCEPT ![nid] = node]

\* Vclock operations
VclockSet(vclock, nid, val) == [vclock EXCEPT ![nid] = val]
VclockGE(a, b) == \A i \in NodeIDs: a[i] >= b[i]

\* Rows of one origin are strictly ordered on every delivery path, so the
\* journal rows of each origin are always a prefix of that origin's rows.
\* The journal vclock - the per-origin count of the rows - then says which
\* entries the node has without scanning the journal. The own component is
\* the LSN of the last own row.
\*
\* The append is also the ack: the applier sends the vclock to the origin
\* right after the write, with the Raft term of that moment. The origin
\* counts the ack only if that term is not above the one it wrote the row in
\* - the one it waits in. The verdict is remembered per origin for its latest
\* row, the only one it can be waiting on, so that the ack keeps its term
\* when the node moves on before the origin gets to count it. The origin's
\* term is on a PROMOTE; a txn is written in the term of the owner's applied
\* PROMOTE, which precedes the txn on every delivery path; a CONFIRM is never
\* waited on. The own rows go the same way: the own write is an ack too.
JournalAppend(entry, node) ==
    LET origin == entry.origin_id
        row_term == CASE entry.type = EntryTypePromote -> entry.raft_term
                      [] entry.type = EntryTypeTransaction -> node.limbo_term_map[origin]
                      [] entry.type = EntryTypeConfirm -> 0
        valid_acks == IF node.raft_term <= row_term
                      THEN node.valid_acks \union {origin}
                      ELSE node.valid_acks \ {origin}
    IN
    IF Assert(entry.lsn = node.journal_vclock[origin] + 1,
              "Rows of one origin arrive strictly in order")
    THEN SetJournal(ArrAppend(entry, node.journal),
         SetJournalVclock(VclockSet(node.journal_vclock, origin, entry.lsn),
         SetValidAcks(valid_acks,
         node)))
    ELSE node

\* The LSN for the next own row.
NextLSN(nid, node) == node.journal_vclock[nid] + 1

\* Transaction fate tracking. Each node decides for itself, exactly once per
\* transaction: a commit appends it to the data, a rollback puts it into the
\* rejected set. Two nodes can disagree only when the replication between
\* them is broken.
TxnIsDecided(txn_data, node) ==
    \/ ArrContains(txn_data, node.data)
    \/ txn_data \in node.data_rejected

SetTxnCommitted(txn_data, node) ==
    IF Assert(~TxnIsDecided(txn_data, node), "A transaction is decided once")
    THEN SetData(ArrAppend(txn_data, node.data), node)
    ELSE node

SetTxnRejected(txn_data, node) ==
    IF Assert(~TxnIsDecided(txn_data, node), "A transaction is decided once")
    THEN SetDataRejected(node.data_rejected \union {txn_data}, node)
    ELSE node

--------------------------------------------------------------------------------
\*
\* Constructors and helpers
\*

\* Create a new transaction entry. It carries no term - the receivers derive
\* it from the origin's component in their limbo term map.
EntryNewTransaction(origin_id, lsn, data) == [
    type |-> EntryTypeTransaction,
    origin_id |-> origin_id,
    lsn |-> lsn,
    data |-> data
]

\* Create a new PROMOTE entry. The term map carries the terms of all the
\* promotions squashed into this one - one PROMOTE can represent a whole
\* chain of them, with terms of different origins, or even multiple terms
\* of the same origin (then only the biggest one is kept).
EntryNewPromote(origin_id, lsn, raft_term, prev_owner, confirm_lsn,
                confirmed_vclock, term_map) == [
    type |-> EntryTypePromote,
    origin_id |-> origin_id,
    lsn |-> lsn,
    raft_term |-> raft_term,
    prev_owner |-> prev_owner,
    confirm_lsn |-> confirm_lsn,
    confirmed_vclock |-> confirmed_vclock,
    term_map |-> term_map
]

\* Create a new CONFIRM entry: the origin's own rows up to confirm_lsn are
\* confirmed. The row at confirm_lsn is a transaction of the origin, or the
\* origin's own PROMOTE - one CONFIRM meaning for both, and the PROMOTE's
\* CONFIRM names the exact row it confirms.
EntryNewConfirm(origin_id, lsn, confirm_lsn) == [
    type |-> EntryTypeConfirm,
    origin_id |-> origin_id,
    lsn |-> lsn,
    confirm_lsn |-> confirm_lsn
]

\* Check if a promotion entry is valid (has origin_id field)
PromoteIsValid(promote) ==
    "origin_id" \in DOMAIN promote

\* Create an invalid/empty promotion entry
PromoteEmpty == [null |-> NULL]

\* Check if a transaction entry is valid (has origin_id field)
TxnIsValid(txn) ==
    "origin_id" \in DOMAIN txn

\* Create an invalid/empty transaction entry
TxnEmpty == [null |-> NULL]

\* Promotions dictionary operations
PromotionsEmpty == [i \in NodeIDs |-> PromoteEmpty]

\* A term hosts at most one PROMOTE. The filters refuse a term taken by
\* another origin, so no two pending promotions ever share a term.
PromotionsSet(nid, promote, promotions) ==
    IF Assert(\A other \in NodeIDs \ {nid}:
                  ~PromoteIsValid(promotions[other]) \/
                  promotions[other].raft_term # promote.raft_term,
              "No two pending promotions share a term")
    THEN [promotions EXCEPT ![nid] = promote]
    ELSE promotions
PromotionsFindPending(nid, lsn, promotions) ==
    LET promote == promotions[nid]
    IN IF PromoteIsValid(promote) /\ promote.lsn = lsn
       THEN promote
       ELSE PromoteEmpty

\* Drop the pending promotions covered by the applied terms: their origin's
\* applied term reached them, meaning the applied promotion was chained from
\* them, or superseded them from the same origin. The others stay, even when
\* they can't get confirmed anymore - their CONFIRM has to be recognized.
PromotionsRemoveCovered(term_map, promotions) ==
    [nid \in DOMAIN(promotions) |->
        IF PromoteIsValid(promotions[nid]) /\ promotions[nid].raft_term <= term_map[nid]
        THEN PromoteEmpty
        ELSE promotions[nid]]

\* Check if node has an entry in its journal
HasEntry(node, entry) ==
    entry.lsn <= node.journal_vclock[entry.origin_id]

\* Replication link operations. The link src -> dst is the applier of dst
\* receiving from src.
LinkIsOpenFromTo(src, dst) == src \in Nodes[dst].appliers

\* The acks for an entry which its origin can count: the nodes having the
\* entry, whose ack carried a term not above the origin's. An ack with a
\* higher term never counts - the relay processes the term first and the
\* origin steps down (relay.cc tx_status_update). The term is the one the
\* node had when it appended the entry, not its current one, see
\* JournalAppend: the ack can be in flight while the node moves to a higher
\* term, and the origin still counts it on arrival. The entry is the origin's
\* latest row - it writes nothing else while waiting - and the origin's own
\* write is always among the acks. The ack travels over the node's applier
\* from the origin: once the node refused something from the origin and
\* closed it, the origin hears nothing from the node anymore, whatever the
\* node receives through others.
CountAcksForEntry(entry) ==
    LET origin == entry.origin_id
    IN
    IF Assert(origin \in Nodes[origin].valid_acks,
              "The origin's own write of the entry is an ack")
    THEN Cardinality({nid \in NodeIDs:
             /\ HasEntry(Nodes[nid], entry)
             /\ origin \in Nodes[nid].valid_acks
             /\ nid = origin \/ LinkIsOpenFromTo(origin, nid)})
    ELSE 0

\* Check if all journal entries from 'from' node are present in 'to' node
JournalIsFullyReplicatedTo(from, to) ==
    VclockGE(to.journal_vclock, from.journal_vclock)

\* Create new node state
NodeNew(nid) == [
    role |-> IF nid \in VoterIDs THEN NodeRoleVoter ELSE NodeRoleCandidate,
    \* The nodes this one receives from. A voter receives no rows, but its
    \* links still carry the terms and the vote requests.
    appliers |-> NodeIDs \ {nid},
    journal |-> <<>>,
    raft_term |-> 1,
    raft_state |-> RaftStateFollower,
    \* The node voted for in the current term, NULL when didn't vote yet. The
    \* votes are not journal entries - they are Raft messages, observed by the
    \* peers over the replication links.
    raft_vote |-> NULL,
    limbo_state |-> LimboStateReplica,
    limbo_term |-> 1,
    limbo_owner |-> InitialLimboOwner,
    limbo_vclock |-> [i \in NodeIDs |-> -1],
    \* Last known term of each node, taken from that node's last confirmed
    \* PROMOTE. Used to find the term of the incoming transactions by their
    \* origin, like the real code does.
    limbo_term_map |-> [i \in NodeIDs |-> 0],
    limbo_promotions |-> PromotionsEmpty,
    limbo |-> TxnEmpty,
    \* The committed transactions, in the commit order.
    data |-> <<>>,
    \* The rolled back transactions. Their order is node-local: a transaction
    \* is rolled back either on receipt or when the queue is cleared, and
    \* nothing across nodes depends on which.
    data_rejected |-> {},
    \* The count of the journal rows of each origin.
    journal_vclock |-> [i \in NodeIDs |-> 0],
    \* The origins whose latest row this node acked with a term not above
    \* the row's own, see JournalAppend.
    valid_acks |-> {},
    \* Whether the node created a transaction since it was elected. A node
    \* creates at most one transaction per term - a state space reduction.
    made_txn |-> FALSE
]

\* Initialize state
Init ==
    /\ Nodes = [nid \in NodeIDs |-> NodeNew(nid)]
    /\ TransactionsToDo = AllTransactions
    /\ PromotesWritten = 0

--------------------------------------------------------------------------------
\*
\* Raft election actions
\*

\* Node randomly bumps its term to start an election, voting for itself and
\* becoming a candidate. A candidate does it too - its round timed out
\* without a quorum. So does a leader - this is how it leaves its leadership
\* on its own, see the Raft states.
NodeBumpTerm(nid) ==
    LET node == Nodes[nid]
    IN
    /\ node.raft_term < MaxTerm
    /\ node.role = NodeRoleCandidate
    \* ---
    /\ Nodes' = NodesUpdate(nid,
                SetRaftTerm(node.raft_term + 1,
                SetRaftVote(nid,
                SetRaftState(RaftStateCandidate,
                SetLimboState(LimboStateReplica,
                node)))))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

\* Node votes for a candidate of its term, once per term. The candidate's
\* journal must contain everything the voter has. The vote request is a
\* Raft message coming over the candidate's replication link.
\*
\* With quorum 1 the self-vote is the quorum: the other votes are never
\* counted, and the only thing a vote does to the voter is forbid its own
\* election in that term - which it may equally just not attempt. So the
\* votes are not even cast then, a state space reduction.
NodeVote(voter_nid, cand_nid) ==
    LET voter == Nodes[voter_nid]
        cand == Nodes[cand_nid]
    IN
    /\ Quorum > 1
    /\ voter_nid # cand_nid
    /\ voter.raft_term = cand.raft_term
    /\ voter.raft_vote = NULL
    /\ cand.raft_state = RaftStateCandidate
    /\ Assert(cand.raft_vote = cand_nid, "A candidate has voted for itself")
    /\ JournalIsFullyReplicatedTo(voter, cand)
    /\ LinkIsOpenFromTo(cand_nid, voter_nid)
    \* ---
    /\ Nodes' = NodesUpdate(voter_nid, SetRaftVote(cand_nid, voter))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

\* Node becomes Raft leader after having a quorum of votes: its own one and
\* the Raft messages coming over the voters' replication links.
NodeBecomeLeader(nid) ==
    LET node == Nodes[nid]
        term == node.raft_term
    IN
    /\ node.raft_state = RaftStateCandidate
    /\ Assert(node.role = NodeRoleCandidate, "A voter is never a candidate")
    /\ Assert(node.raft_vote = nid, "A candidate has voted for itself")
    /\ Cardinality({other \in NodeIDs:
            /\ Nodes[other].raft_term = term
            /\ Nodes[other].raft_vote = nid
            /\ other = nid \/ LinkIsOpenFromTo(other, nid)}) >= Quorum
    \* ---
    /\ Nodes' = NodesUpdate(nid,
                SetRaftState(RaftStateLeader,
                SetMadeTxn(FALSE,
                node)))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

\* Node observes a higher term from another node and steps down. The vote is
\* reset - it is per term. The terms travel both with the entries and with
\* the acks, so one open link in any direction is enough. The step is per
\* term, not per node it is seen from: the result is the same whoever has
\* it, so the duplicates are not even built.
NodeObserveHigherTerm(nid) ==
    LET node == Nodes[nid]
    IN
    \* Cheap check first: the sets below are built only when there is a
    \* higher term somewhere at all. Not an \E - TLC splits an \E conjunct of
    \* an action into one successor per witness, duplicates included.
    /\ ~\A other \in NodeIDs: Nodes[other].raft_term <= node.raft_term
    /\ LET peers == {other \in NodeIDs \ {nid}:
                        LinkIsOpenFromTo(other, nid) \/ LinkIsOpenFromTo(nid, other)}
           terms == {Nodes[other].raft_term : other \in peers}
       IN
       \E term \in terms:
        /\ term > node.raft_term
        \* ---
        /\ Nodes' = NodesUpdate(nid,
                    SetRaftTerm(term,
                    SetRaftVote(NULL,
                    SetRaftState(RaftStateFollower,
                    SetLimboState(LimboStateReplica,
                    node)))))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

--------------------------------------------------------------------------------
\*
\* Limbo PROMOTE actions
\*

\* A pending PROMOTE is poisoned when no CONFIRM can apply it on this node:
\* the limbo moved past its term without it, or an applied PROMOTE carries a
\* term of some node which the entry's author didn't know, or it confirms
\* less than this node has confirmed already. It appears in any
\* cluster, when a leader loses the leadership before its PROMOTE reaches
\* anybody. With a majority quorum it never gets confirmed - a confirmed
\* PROMOTE is known to every later leader. With a smaller quorum it can, by a
\* leader elected without intersecting the quorum which confirmed the older
\* PROMOTE. That is a fork, and its CONFIRM is refused.
PromoteIsPoisoned(node, promote) ==
    \/ promote.raft_term <= node.limbo_term
    \/ ~VclockGE(promote.term_map, node.limbo_term_map)
    \* Confirms less than what is confirmed here already - another fork
    \* signature, and permanent like the others: the confirmed vclock only
    \* grows. It has to be poison and not only a CONFIRM-time refusal: a
    \* chain over such a PROMOTE adopts a forked leadership into its term map
    \* and, writing its own row into the inherited vclock, hides the very
    \* component which showed the fork. Found by simulation on 04.
    \/ ~VclockGE(promote.confirmed_vclock, node.limbo_vclock)

\* The filter of a CONFIRM applying a pending PROMOTE, own or received: a
\* poisoned one is a fork. With a majority quorum a pending PROMOTE of the
\* leader always passes - nothing gets confirmed behind the leader's back.
PromoteCanBeConfirmed(node, promote) ==
    ~PromoteIsPoisoned(node, promote)

\* The filter of a PROMOTE entering the journal, own or received. A term
\* hosts at most one PROMOTE - one re-using a term the node knows as taken by
\* another origin, applied or pending, is a second leader elected in the same
\* term.
PromoteCanBeWritten(node, term, origin) ==
    \A nid \in NodeIDs \ {origin}:
        /\ node.limbo_term_map[nid] # term
        /\ PromoteIsValid(node.limbo_promotions[nid]) =>
           node.limbo_promotions[nid].raft_term # term

\* The latest pending promotion not poisoned here. It may still be behind the
\* node's confirmed vclock - written before a CONFIRM applied here, by an
\* owner confirming behind the author's back, a quorum at or below half - and
\* then never confirmable here. A chain from it inherits that, and fails at
\* its own confirmation as the fork it is. The code chains the same way.
PromotionsGetLatestLive(node) ==
    LET promotions == node.limbo_promotions
        live == {nid \in DOMAIN(promotions):
                    /\ PromoteIsValid(promotions[nid])
                    /\ ~PromoteIsPoisoned(node, promotions[nid])}
    IN IF live = {}
       THEN PromoteEmpty
       ELSE promotions[CHOOSE nid \in live:
                \A other \in live:
                    promotions[nid].raft_term >= promotions[other].raft_term]

\* Whether the journal has a PROMOTE of the given term from another origin -
\* a second leader elected in the same term.
JournalHasForeignPromoteOfTerm(journal, term, origin) ==
    \E i \in DOMAIN(journal):
        /\ journal[i].type = EntryTypePromote
        /\ journal[i].raft_term = term
        /\ journal[i].origin_id # origin

\* Store a PROMOTE, own or received, as the pending promotion of its origin
\* and in the journal. Rows of one origin are strictly ordered on every
\* delivery path, so a PROMOTE never goes back in its origin's history.
LimboStorePromote(entry, node) ==
    LET origin == entry.origin_id
        old_promote == node.limbo_promotions[origin]
    IN
    IF /\ Assert(PromoteCanBeWritten(node, entry.raft_term, origin),
                 "Only a PROMOTE passing the filter is stored")
       /\ Assert(~PromoteIsValid(old_promote) \/ old_promote.raft_term < entry.raft_term,
                 "New PROMOTE must have term > old PROMOTE term")
       /\ Assert(~PromoteIsValid(old_promote) \/ old_promote.lsn < entry.lsn,
                 "New PROMOTE must have LSN > old PROMOTE LSN")
       /\ Assert(~IsMajorityQuorum \/
                 ~JournalHasForeignPromoteOfTerm(node.journal, entry.raft_term, origin),
                 "A majority quorum elects one leader per term")
    THEN SetLimboPromotions(PromotionsSet(origin, entry, node.limbo_promotions),
         JournalAppend(entry, node))
    ELSE node

\* Apply a pending PROMOTE by its CONFIRM, own or received: decide the
\* queued transaction by the PROMOTE's confirm boundary, move the limbo to
\* the PROMOTE's term, owner and vclocks, drop the covered pending
\* promotions, and journal the CONFIRM.
LimboApplyPromote(promote, confirm_entry, node) ==
    LET has_txn == TxnIsValid(node.limbo)
        txn_entry == node.limbo
        owner_matches == has_txn /\ txn_entry.origin_id = promote.prev_owner
        should_commit == owner_matches /\ txn_entry.lsn <= promote.confirm_lsn
        decided_node == IF ~has_txn THEN node
                        ELSE IF should_commit THEN SetTxnCommitted(txn_entry.data, node)
                        ELSE SetTxnRejected(txn_entry.data, node)
    IN
    IF /\ Assert(PromoteCanBeConfirmed(node, promote),
                 "Only a PROMOTE passing the filter is applied")
       /\ Assert(has_txn => txn_entry.origin_id = node.limbo_owner,
                 "Transaction origin must match current limbo owner")
       /\ Assert(\E i \in NodeIDs: promote.term_map[i] > node.limbo_term_map[i],
                 "PROMOTE's term map must advance at least one component")
    THEN SetLimboTerm(promote.raft_term,
         SetLimboOwner(promote.origin_id,
         SetLimboVclock(promote.confirmed_vclock,
         SetLimboTermMap(promote.term_map,
         SetLimboPromotions(PromotionsRemoveCovered(promote.term_map, node.limbo_promotions),
         SetLimbo(TxnEmpty,
         JournalAppend(confirm_entry, decided_node)))))))
    ELSE node

\* Raft leader writes PROMOTE right after winning the elections, without
\* waiting for the pending txns to reach a quorum of replicas. The elections
\* guarantee the leader already has every entry which could have gathered a
\* quorum anywhere, and the quorum on the PROMOTE itself transitively
\* certifies the leader's whole journal before anything gets confirmed -
\* the PROMOTE is the last journal entry and the replication is strictly
\* ordered.
LimboWritePromote(nid) ==
    LET node == Nodes[nid]
    IN
    /\ PromotesWritten < MaxPromotes
    /\ node.raft_state = RaftStateLeader
    /\ node.raft_term > node.limbo_term
    \* A pending PROMOTE at or above the own term means the elections have
    \* moved on already - the term can overtake the PROMOTE on the way, so the
    \* leader might not know it yet. Chaining from such a PROMOTE would put a
    \* term above the own one into the term map. The promotion is not even
    \* attempted.
    /\ \A other \in NodeIDs:
        PromoteIsValid(node.limbo_promotions[other]) =>
        node.limbo_promotions[other].raft_term < node.raft_term
    \* The own PROMOTE goes through the same filter as the received ones.
    /\ PromoteCanBeWritten(node, node.raft_term, nid)
    \* ---
    \* Chained promotion. An older pending PROMOTE, even one from another
    \* node, can still get confirmed after this new PROMOTE is written. The
    \* new PROMOTE must then lead to the exact same state when its own
    \* CONFIRM is applied. A poisoned one can't get confirmed here, so there
    \* is nothing to stay consistent with - chaining from it would only
    \* inherit the poison.
    /\ LET latest_promote == PromotionsGetLatestLive(node)
           has_pending == PromoteIsValid(latest_promote)
           \* prev_owner: use latest (previous) promote's origin_id if exists, otherwise current limbo owner
           prev_owner == IF has_pending
                         THEN latest_promote.prev_owner
                         ELSE node.limbo_owner
           \* confirm_lsn: use latest (previous) promote's if exists, otherwise limbo/vclock
           confirm_lsn == IF has_pending
                          THEN latest_promote.confirm_lsn
                          ELSE IF TxnIsValid(node.limbo)
                               THEN node.limbo.lsn
                               ELSE node.limbo_vclock[node.limbo_owner]
           \* confirmed_vclock: the chain's as it is - the verdict on the
           \* previous owner was written by the first PROMOTE of the chain,
           \* and nothing confirms more for that owner while its succession
           \* is pending - or the own limbo vclock with the verdict.
           base_vclock == IF has_pending
                          THEN latest_promote.confirmed_vclock
                          ELSE VclockSet(node.limbo_vclock, prev_owner, confirm_lsn)
           \* term_map: accumulate the chain's terms the same way as the
           \* confirmed vclock, folding this promotion's own term in
           base_term_map == IF has_pending
                            THEN latest_promote.term_map
                            ELSE node.limbo_term_map
           lsn == NextLSN(nid, node)
           \* The row itself is in its confirmed vclock: its CONFIRM names
           \* it, and a chain over it carries it on, so a late CONFIRM of a
           \* covered PROMOTE is recognized as already done.
           entry == EntryNewPromote(
               nid,
               lsn,
               node.raft_term,
               prev_owner,
               confirm_lsn,
               VclockSet(base_vclock, nid, lsn),
               VclockSet(base_term_map, nid, node.raft_term)
           )
       IN Nodes' = NodesUpdate(nid, LimboStorePromote(entry, node))
    /\ PromotesWritten' = PromotesWritten + 1
    /\ UNCHANGED TransactionsToDo

\* Leader confirms PROMOTE after quorum receives it. The own CONFIRM goes
\* through the same filter as the received ones - it is never written when it
\* would be refused right here.
LimboConfirmPromote(nid) ==
    LET node == Nodes[nid]
        promote_entry == node.limbo_promotions[nid]
    IN
    /\ node.raft_state = RaftStateLeader
    /\ PromoteIsValid(promote_entry)
    /\ promote_entry.raft_term = node.raft_term
    \* The own PROMOTE poisoned by a newer applied promotion is the leader
    \* overtaken before observing the term. By an older one - a fork.
    /\ Assert(\/ ~IsMajorityQuorum
              \/ node.limbo_term > promote_entry.raft_term
              \/ PromoteCanBeConfirmed(node, promote_entry),
              "A majority quorum never confirms behind the leader's back")
    /\ PromoteCanBeConfirmed(node, promote_entry)
    /\ CountAcksForEntry(promote_entry) >= Quorum
    \* ---
    /\ Assert(promote_entry.raft_term > node.limbo_term,
              "Local pending promote's term is always bigger than the last confirmed limbo term")
    /\ LET confirm_entry == EntryNewConfirm(
               nid,
               NextLSN(nid, node),
               promote_entry.lsn
           )
       IN Nodes' = NodesUpdate(nid,
                   SetLimboState(LimboStateLeader,
                   LimboApplyPromote(promote_entry, confirm_entry, node)))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

--------------------------------------------------------------------------------
\*
\* Limbo transaction actions
\*

\* Limbo leader creates a new transaction
LimboCreateTransaction(nid) ==
    LET node == Nodes[nid]
    IN
    /\ node.limbo_state = LimboStateLeader
    /\ ~TxnIsValid(node.limbo)
    /\ ~node.made_txn
    /\ TransactionsToDo # {}
    \* ---
    \* The transactions are interchangeable until created, so any one of the
    \* remaining ones is as good as another: a deterministic pick instead of
    \* a successor per transaction. The symmetry on the transactions stays
    \* needed - the pick fixes the name of the first transaction created, not
    \* who creates it, and the two orders of creators are each other's
    \* renaming.
    /\ LET txn_data == CHOOSE t \in TransactionsToDo: TRUE
           entry == EntryNewTransaction(nid, NextLSN(nid, node), txn_data)
       IN
        /\ TransactionsToDo' = TransactionsToDo \ {txn_data}
        /\ UNCHANGED PromotesWritten
        /\ Nodes' = NodesUpdate(nid,
                    SetLimbo(entry,
                    SetMadeTxn(TRUE,
                    JournalAppend(entry, node))))

\* Limbo leader confirms transaction after quorum receives it
LimboConfirmTransaction(nid) ==
    LET node == Nodes[nid]
        txn_entry == node.limbo
    IN
    /\ node.limbo_state = LimboStateLeader
    /\ TxnIsValid(node.limbo)
    /\ CountAcksForEntry(txn_entry) >= Quorum
    \* ---
    /\ LET confirm_entry == EntryNewConfirm(
               nid,
               NextLSN(nid, node),
               txn_entry.lsn
           )
       IN
       /\ Assert(txn_entry.lsn >= node.limbo_vclock[nid],
                 "Vclock LSN must not decrease")
       /\ Nodes' = NodesUpdate(nid,
                    SetLimbo(TxnEmpty,
                    SetLimboVclock(VclockSet(node.limbo_vclock, nid, txn_entry.lsn),
                    JournalAppend(confirm_entry,
                    SetTxnCommitted(txn_entry.data, node)))))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

--------------------------------------------------------------------------------
\*
\* Replication actions
\*

\* Refuse an entry as a split brain. The applier stops and never delivers
\* anything from src again - neither entries nor terms.
LinkCloseFromTo(src, dst) ==
    LET node == Nodes[dst]
    IN
    /\ Assert(~IsMajorityQuorum, "A majority quorum never refuses anything")
    /\ Nodes' = NodesUpdate(dst, SetAppliers(node.appliers \ {src}, node))

\* Refuse a row offered by several sources: it came over one of the links,
\* and that one closes - a branch per source.
LinkCloseFromAnyOfTo(srcs, dst) ==
    \E src \in srcs: LinkCloseFromTo(src, dst)

\* Find the next journal entry to replicate (first one not present on destination)
\* Returns 0 if destination has all entries, otherwise returns the index
\* TODO: makes sense to make a binary search?
RECURSIVE FirstMissingEntryIdx(_, _, _)
FirstMissingEntryIdx(journal, dst_node, i) ==
    IF i > Len(journal) THEN 0
    ELSE IF ~HasEntry(dst_node, journal[i]) THEN i
    ELSE FirstMissingEntryIdx(journal, dst_node, i + 1)

NextEntryToReplicate(src_node, dst_node) ==
    IF JournalIsFullyReplicatedTo(src_node, dst_node)
    THEN 0
    ELSE FirstMissingEntryIdx(src_node.journal, dst_node, 1)

\* Apply a PROMOTE entry to destination node. It is stored as pending even
\* when poisoned already - an old PROMOTE, superseded before getting
\* confirmed, can show up late through another link, and its CONFIRM must
\* be recognized. A pending PROMOTE, however new, changes nothing about the
\* leadership: the limbo leader is demoted by the Raft term it observes or
\* by the CONFIRM it applies, see LimboStateInvariant.
ReplicatePromote(entry, srcs, dst_nid) ==
    LET dst_node == Nodes[dst_nid]
        origin == entry.origin_id
    IN
    IF ~PromoteCanBeWritten(dst_node, entry.raft_term, origin)
    THEN LinkCloseFromAnyOfTo(srcs, dst_nid)
    ELSE
        \* A chain carrying this PROMOTE's term is always delivered after it,
        \* so it is never covered on arrival.
        /\ Assert(entry.raft_term > dst_node.limbo_term_map[origin],
                  "PROMOTE can't be covered on arrival")
        /\ Nodes' = NodesUpdate(dst_nid, LimboStorePromote(entry, dst_node))

\* Apply CONFIRM on PROMOTE entry to destination node. The CONFIRM of a
\* PROMOTE not passing the filter is a fork - its author confirmed it without
\* knowing something applied here.
ReplicateConfirmPromote(entry, srcs, dst_nid, promote) ==
    LET dst_node == Nodes[dst_nid]
    IN
    /\ Assert(entry.confirm_lsn = promote.lsn,
             "The CONFIRM names the PROMOTE's own row")
    /\ IF ~PromoteCanBeConfirmed(dst_node, promote)
       THEN LinkCloseFromAnyOfTo(srcs, dst_nid)
       ELSE
           \* The ownership went to the PROMOTE's origin. A limbo leader
           \* which confirmed its own PROMOTE with this newer one pending
           \* is a replica again.
           LET new_node == SetLimboState(LimboStateReplica,
                           LimboApplyPromote(promote, entry, dst_node))
               own_promote == new_node.limbo_promotions[dst_nid]
           IN
           \* A leader's own pending PROMOTE stays confirmable through an
           \* older promotion applied beside it - a majority confirms
           \* nothing behind its back. A newer one is a different thing: the
           \* leader got overtaken, it just hasn't observed the term yet,
           \* the term can travel behind the PROMOTE.
           /\ Assert(\/ ~IsMajorityQuorum
                     \/ new_node.raft_state # RaftStateLeader
                     \/ ~PromoteIsValid(own_promote)
                     \/ promote.raft_term > own_promote.raft_term
                     \/ PromoteCanBeConfirmed(new_node, own_promote),
                     "A majority quorum never confirms behind the leader's back")
           /\ Nodes' = NodesUpdate(dst_nid, new_node)

\* Apply a CONFIRM entry to destination node: the origin's rows up to
\* confirm_lsn are confirmed. The row named is the origin's pending PROMOTE,
\* or a row already covered here, or the owner's queued transaction;
\* anything else is a fork - an old owner still confirming, or a confirmation
\* of what isn't queued here at all.
ReplicateConfirm(entry, srcs, dst_nid) ==
    LET dst_node == Nodes[dst_nid]
        origin == entry.origin_id
        current_lsn == dst_node.limbo_vclock[origin]
        pending == dst_node.limbo_promotions[origin]
        promote == PromotionsFindPending(origin, entry.confirm_lsn, dst_node.limbo_promotions)
        txn_entry == dst_node.limbo
        is_txn_confirm ==
            /\ origin = dst_node.limbo_owner
            /\ TxnIsValid(txn_entry)
            /\ txn_entry.lsn <= entry.confirm_lsn
    IN
    \* The rows of one origin arrive in order, and a PROMOTE's CONFIRM comes
    \* before anything written after the PROMOTE. So while the origin has a
    \* PROMOTE pending here, the only CONFIRM of its that can arrive is that
    \* PROMOTE's own.
    /\ Assert(~PromoteIsValid(pending) \/ pending.lsn = entry.confirm_lsn,
              "A CONFIRM from an origin with a pending PROMOTE is that PROMOTE's")
    /\ IF PromoteIsValid(promote)
       THEN ReplicateConfirmPromote(entry, srcs, dst_nid, promote)
       ELSE IF entry.confirm_lsn <= current_lsn
       THEN Nodes' = NodesUpdate(dst_nid, JournalAppend(entry, dst_node))
       ELSE IF is_txn_confirm
       THEN /\ Assert(txn_entry.origin_id = origin,
                      "The queued transaction is the owner's")
            /\ Nodes' = NodesUpdate(dst_nid,
                         SetLimboVclock(VclockSet(dst_node.limbo_vclock, origin, entry.confirm_lsn),
                         SetLimbo(TxnEmpty,
                         JournalAppend(entry,
                         SetTxnCommitted(txn_entry.data, dst_node)))))
       ELSE LinkCloseFromAnyOfTo(srcs, dst_nid)

\* Apply a transaction entry to destination node
ReplicateTransaction(entry, dst_nid) ==
    LET dst_node == Nodes[dst_nid]
        is_from_owner == entry.origin_id = dst_node.limbo_owner
        \* The entry carries no term. The origin's term is derived from the
        \* local term map - the term of the origin's last confirmed PROMOTE.
        \* The ordered replication makes this equal to attaching the creation
        \* term to the txn: the txn is always received after the origin's
        \* promote confirmation and before any newer rows of that origin.
        is_old_term == dst_node.limbo_term_map[entry.origin_id] < dst_node.limbo_term
    IN
    IF is_from_owner
    THEN
        \* The owner writes a new transaction only after the previous one is
        \* confirmed, and the CONFIRM precedes it on every delivery path.
        /\ Assert(~TxnIsValid(dst_node.limbo),
                  "The owner's transactions are queued one at a time")
        /\ Nodes' = NodesUpdate(dst_nid,
                     SetLimbo(entry,
                     JournalAppend(entry, dst_node)))
    ELSE IF is_old_term
    THEN
        \* Rollback old term transaction
        Nodes' = NodesUpdate(dst_nid,
                  JournalAppend(entry,
                  SetTxnRejected(entry.data, dst_node)))
    ELSE
        \* A transaction from a non-owner in the current term can't come: the
        \* owner's applied term is the limbo's, and every other origin's is
        \* below it (LimboTermMapInvariant). A fork shows on the PROMOTE
        \* preceding the transaction in its origin's journal, refused first.
        /\ Assert(FALSE, "A non-owner's transaction is always from an old term")
        /\ UNCHANGED Nodes

\* Main replication action: an applier of the node applies the next entry
\* from its source. The sources often offer the same row - the first one the
\* node lacks - and applying it doesn't depend on who sent it, so the step is
\* per row, each row once however many sources offer it. Only a refusal
\* depends on the source: it closes the link the row came over, so a refused
\* row branches over its sources.
ReplicateNextEntry(dst_nid) ==
    LET dst_node == Nodes[dst_nid]
        \* Each open source with the index of its first row missing here.
        offers == {<<src, NextEntryToReplicate(Nodes[src], dst_node)>> :
                      src \in dst_node.appliers}
        pending == {o \in offers : o[2] # 0}
        entries == {Nodes[o[1]].journal[o[2]] : o \in pending}
    IN
    /\ dst_node.role # NodeRoleVoter
    /\ \E entry \in entries:
        LET srcs == {o[1] : o \in {p \in pending : Nodes[p[1]].journal[p[2]] = entry}}
        IN
        CASE entry.type = EntryTypePromote -> ReplicatePromote(entry, srcs, dst_nid)
          [] entry.type = EntryTypeConfirm -> ReplicateConfirm(entry, srcs, dst_nid)
          [] entry.type = EntryTypeTransaction -> ReplicateTransaction(entry, dst_nid)
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>
--------------------------------------------------------------------------------
\*
\* Main specification
\*

Next ==
    \/ \E nid \in NodeIDs: NodeBumpTerm(nid)
    \/ \E nid \in NodeIDs, cand \in NodeIDs: NodeVote(nid, cand)
    \/ \E nid \in NodeIDs: NodeBecomeLeader(nid)
    \/ \E nid \in NodeIDs: NodeObserveHigherTerm(nid)
    \/ \E nid \in NodeIDs: LimboWritePromote(nid)
    \/ \E nid \in NodeIDs: LimboConfirmPromote(nid)
    \/ \E nid \in NodeIDs: LimboCreateTransaction(nid)
    \/ \E nid \in NodeIDs: LimboConfirmTransaction(nid)
    \/ \E nid \in NodeIDs: ReplicateNextEntry(nid)

--------------------------------------------------------------------------------
\*
\* Invariants
\*

\* Whether dst has every row src has, whatever the path was.
IsCaughtUp(src, dst) ==
    JournalIsFullyReplicatedTo(Nodes[src], Nodes[dst])

\* Everything a has decided, b has decided the same way: a's commits are a
\* prefix of b's commits, a's rejections are among b's rejections.
TxnDecisionsAreCoveredBy(a, b) ==
    LET na == Nodes[a]
        nb == Nodes[b]
    IN
    /\ Len(na.data) <= Len(nb.data)
    /\ \A i \in DOMAIN(na.data): na.data[i] = nb.data[i]
    /\ na.data_rejected \subseteq nb.data_rejected

\* Every leadership a has confirmed, b has confirmed too, or moved past: each
\* origin's applied term, and the limbo term.
LeadershipIsCoveredBy(a, b) ==
    LET na == Nodes[a]
        nb == Nodes[b]
    IN
    /\ na.limbo_term <= nb.limbo_term
    /\ VclockGE(nb.limbo_term_map, na.limbo_term_map)

\* Consistency over a live link: a divergence can exist only while the row
\* revealing it is in flight. Every decision is made by a journal row, and a
\* refused row is never journaled. So once dst has every row src has, it made
\* every decision src made, the same way, and confirmed every leadership src
\* confirmed. A conflict - a transaction decided differently, a term
\* confirmed for two origins - is always on a row the receiver refuses. With
\* a majority quorum nothing is refused, so this is every pair, always.
CaughtUpConsistencyInvariant ==
    \A a \in DataNodes, b \in DataNodes:
        a # b /\ IsCaughtUp(a, b) =>
            /\ TxnDecisionsAreCoveredBy(a, b)
            /\ LeadershipIsCoveredBy(a, b)

\* Nothing is left to deliver over the open links. Used by the witnesses.
Quiescent ==
    \A src \in DataNodes, dst \in DataNodes:
        src # dst => ~LinkIsOpenFromTo(src, dst) \/ IsCaughtUp(src, dst)

\* The state derived from the journal, excluding the Raft-dependent parts.
StatesEqual(a, b) ==
    LET na == Nodes[a]
        nb == Nodes[b]
    IN
    /\ na.limbo_term = nb.limbo_term
    /\ na.limbo_owner = nb.limbo_owner
    /\ na.limbo_vclock = nb.limbo_vclock
    /\ na.limbo_term_map = nb.limbo_term_map
    /\ na.limbo_promotions = nb.limbo_promotions
    /\ na.limbo = nb.limbo
    /\ na.data = nb.data
    /\ na.data_rejected = nb.data_rejected

\* Two nodes holding the same rows have the same state derived from them,
\* whatever order the rows came in and whatever the links did meanwhile: the
\* journal determines the limbo. With CaughtUpConsistencyInvariant - the
\* receiver covers the sender - this is also all that can be said about the
\* links, as the fork detection is not always two-sided. Two leaders of one
\* term (a quorum at or below half) refuse each other's PROMOTE only while
\* their own one for that term is pending. A leader re-elected before its
\* PROMOTE got confirmed chains the new one over it and forgets the old term,
\* so it stores the other leader's PROMOTE as live, and learns of the fork
\* only from that leader's CONFIRM, which poisons its own pending by
\* regression - or never, if that CONFIRM doesn't come, and then it just
\* follows. It confirmed nothing of its own in between, so the one-way link
\* breaks nothing: the receiver has everything the sender has.
JournalDeterminesStateInvariant ==
    \A a \in DataNodes, b \in DataNodes:
        a # b /\ IsCaughtUp(a, b) /\ IsCaughtUp(b, a) => StatesEqual(a, b)

\* Journal length must not exceed expected maximum
JournalLengthInvariant ==
    \A nid \in NodeIDs:
        Len(Nodes[nid].journal) <= MaxJournalLength

\* The limbo state is a stored field, as in the code where it is kept for a
\* fast check, so it must always equal what the code derives it from
\* (txn_limbo_update_state): the node is the limbo leader exactly when it
\* owns the limbo, is the Raft leader, and the limbo's term is the Raft
\* term. Both directions - the field is updated in the right places, and
\* nowhere else.
LimboStateInvariant ==
    \A nid \in NodeIDs:
        LET node == Nodes[nid]
        IN node.limbo_state = LimboStateLeader <=>
            /\ node.raft_state = RaftStateLeader
            /\ node.limbo_owner = nid
            /\ node.limbo_term = node.raft_term

\* If limbo has a transaction, its origin must match limbo owner
LimboOwnerInvariant ==
    \A nid \in NodeIDs:
        LET node == Nodes[nid]
        IN TxnIsValid(node.limbo) =>
            node.limbo.origin_id = node.limbo_owner

\* The applied term map against the applied limbo term. The map is the one of
\* the applied PROMOTE, whose own term is the highest in it and is the limbo
\* term, so no applied term is above the limbo's, and the owner's is the
\* limbo's - once a PROMOTE is applied at all, the initial owner has none. A
\* term hosts at most one confirmed PROMOTE: no node holds one applied term
\* for two origins. The filters keep it so - a PROMOTE of a term taken in the
\* map is refused, and a pending PROMOTE at or below the applied limbo term is
\* poisoned, its CONFIRM refused. Together: a non-owner's applied term is
\* strictly below the limbo's, which is what makes a non-owner's transaction
\* always an old-term one.
LimboTermMapInvariant ==
    \A nid \in NodeIDs:
        LET node == Nodes[nid]
            map == node.limbo_term_map
        IN
        /\ \A o \in NodeIDs: map[o] <= node.limbo_term
        /\ node.limbo_term > 1 => map[node.limbo_owner] = node.limbo_term
        /\ \A a \in NodeIDs, b \in NodeIDs:
            a # b /\ map[a] > 0 => map[a] # map[b]

TotalInvariant ==
    /\ CaughtUpConsistencyInvariant
    /\ JournalDeterminesStateInvariant
    /\ JournalLengthInvariant
    /\ LimboStateInvariant
    /\ LimboOwnerInvariant
    /\ LimboTermMapInvariant

Spec ==
    /\ Init
    /\ [][Next]_vars
    /\ WF_vars(Next)

================================================================================
