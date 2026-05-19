--------------------------------- MODULE VRaft ---------------------------------

EXTENDS TLC, Integers, Sequences, FiniteSets

--------------------------------------------------------------------------------
\*
\* Constants
\*

\* Node IDs in the cluster.
CONSTANT NodeIDs
CONSTANT VoterIDs

\* All transactions to execute. Each transaction is just a unique identifier of
\* any sort.
CONSTANT AllTransactions

\* Maximum term allowed before stopping.
CONSTANT MaxTerm

\* The PROMOTEs the whole cluster may write. The natural maximum is one per
\* node per electable term, data nodes * (MaxTerm - 1); anything at or above
\* it never binds. A lower value cuts the breadth of the big runs - not every
\* node promotes in every term - and keeps the depth. Large scenarios without a
\* limit can run out of time and resources of even very powerful machines.
CONSTANT MaxPromotes

\* Initial limbo owner (must be in NodeIDs)
CONSTANT InitialLimboOwner

\* Decision quorum. A majority makes any two quorums intersect. A smaller one
\* lets two leaders get elected by disjoint supporters, even in the same term -
\* a fork the protocol can't prevent, and must detect.
CONSTANT Quorum

\* Null value for unset objects of all sorts.
CONSTANT NULL

\* Entry types.
CONSTANT EntryTypeTransaction
CONSTANT EntryTypePromote
CONSTANT EntryTypeConfirm

\* Follower is a read-only node which receives transactions from the leader and
\* no intention to become one yet.
CONSTANT ElectionStateFollower
\* Candidate is a read-only node which is attempting to get elected as a new
\* leader in a new term that it has bumped just recently. It is collecting votes
\* from other nodes (and +1 from self) until it reaches the quorum and declares
\* itself a leader of the current term.
CONSTANT ElectionStateCandidate
\* Leader is a node which has collected enough votes for itself in the current
\* term. Note, that just being the elected leader isn't enough to make the node
\* writeable. It must first promote itself.
CONSTANT ElectionStateLeader

\* Limbo is a replica when it is read-only due to any reason, like when it isn't
\* owned in the current term by the current node. Or owned, but the node is no
\* longer an election leader.
CONSTANT LimboStateReplica
\* Limbo is the leader when the node is writable, can produce and confirm
\* transactions. This state is the result of the election being won in the
\* current term and then the node promoting itself successfully.
CONSTANT LimboStateLeader

\* Candidate node can potentially be the leader. It contains and replicates the
\* data.
CONSTANT NodeRoleCandidate
\* Voter is a node which only keeps track of the terms and can vote for
\* candidate-nodes when they are trying to get elected. Voter nodes perfectly
\* fine fit into VRaft and Raft protocols. In reality they can help the cluster
\* to make decisions when it needs not so many data-replicas, but wants to have
\* a proper quorum. Like when data-nodes are just 2, VRaft/Raft aren't very well
\* applicable, but with a third node-voter it makes more sense.
\*
\* In this spec the voters help to reduce the state-space of the model, since
\* their own state is very small due to not storing any data besides the votes.
\* Voters help to cover traces where a node is not important and is only needed
\* to get some other node elected, while a third node doesn't see it.
CONSTANT NodeRoleVoter

\* The nodes receiving replication.
DataNodes == NodeIDs \ VoterIDs

\* Maximum expected journal length per node. That helps to make sure the traces
\* don't grow infinitely due to some bug in the model.
MaxJournalLength ==
    (MaxTerm * 2 + Cardinality(AllTransactions) * 2) * Cardinality(NodeIDs)

\* Symmetry. The easiest it applies to objects/states which have no meaning
\* other than unique identification of something. For example, in any trace 2
\* nodes A_N1 and B_N2 can swap their ids ID1 and ID2 and nothing will change.
\* Ordering does apply to terms and LSNs, which are plain numbers and remain
\* not symmetric.
Perms == Permutations(DataNodes) \union Permutations(VoterIDs)
         \union Permutations(AllTransactions)

\* With a majority quorum two majorities intersect, forks and split-brains are
\* expected to be impossible. Knowing this allows to add stricter checks for
\* the majority-driven traces, like never tolerating nodes conflicting on any
\* rows. With a <= 50% quorum things like forks are allowed, but other rules
\* get checked stricter, like that the forks are detected and result into
\* disconnects.
IsMajorityQuorum == 2 * Quorum > Cardinality(NodeIDs)

--------------------------------------------------------------------------------
\*
\* Variables
\*

\* Per-node state.
VARIABLE Nodes

\* Remaining transactions to create.
VARIABLE TransactionsToDo

\* The PROMOTEs written so far, all nodes together. It could be deduced from
\* the node journals, but that would be expensive to calculate.
VARIABLE PromotesWritten

vars == <<Nodes, TransactionsToDo, PromotesWritten>>

--------------------------------------------------------------------------------
\*
\* Simple helper functions for object field manipulation
\*

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

ArrAppend(v, s) == Append(s, v)
ArrContains(v, s) == \E i \in DOMAIN(s): s[i] = v

NodesUpdate(nid, node) == [Nodes EXCEPT ![nid] = node]

VclockSet(vclock, nid, val) == [vclock EXCEPT ![nid] = val]
VclockGE(a, b) == \A i \in NodeIDs: a[i] >= b[i]

--------------------------------------------------------------------------------
\*
\* Journal
\*

JournalNextLSN(nid, node) == node.journal_vclock[nid] + 1

\* Rows of one origin are strictly ordered on every delivery path, so the
\* journal rows of each origin are always a prefix of that origin's rows. The
\* journal vclock - the per-origin count of the rows - then says which entries
\* the node has without scanning the journal. The own component is the LSN of
\* the last own row.
\*
\* The append is also the ack: the applier sends its vclock to the origin after
\* the write, with the election term of that moment. The vclock will tell the
\* origin up to which LSN its rows were acked.
\*
\* The origin counts the ack only if that term is not above the one it wrote the
\* row in. A higher term means the receiver has moved on to a newer term by now
\* and that term might have another leader already.
\*
\* Normally, acks are their own network messages. But actually simulating them
\* would require network queues and ack messages, which can also pile up, and
\* this would blow up the state space to an unverifiable size.
\*
\* Instead, the pending usable ack is remembered by the receiver, per each
\* possible sender, and the sender can consume it at a random time. This covers
\* the same randomness of the moment when an ack can be delivered and the
\* existence of invalid acks which simply aren't even "sent", while not
\* exploding the state space.
\*
\* Note that the acks carrying a higher term have another use - they let the
\* old leader see the existence of a newer term, and the leader then steps down.
\* This is still covered - a separate action allows nodes to "observe" a higher
\* term from their neighbours, via acks or heartbeats or any other means that
\* the nodes might be sending in the actual implementation.
JournalAppend(entry, node) ==
    LET origin == entry.origin_id
        origin_term == node.limbo_term_map[origin]
        row_term == CASE entry.type = EntryTypePromote -> entry.raft_term
                      [] entry.type = EntryTypeTransaction -> origin_term
                      [] entry.type = EntryTypeConfirm -> 0
        valid_acks == IF node.raft_term <= row_term
                      \* The term is not exceeding the origin's. The origin will
                      \* 'receive' this ack eventually.
                      THEN node.valid_acks \union {origin}
                      \* The term is newer than of the origin. An ack of a
                      \* higher term is ignored in quorum counting, so 'sending'
                      \* it is pointless. In reality it is actually sent, but
                      \* supposed to be skipped on the receiver by the
                      \* implementation.
                      ELSE node.valid_acks \ {origin}
    IN
    IF Assert(entry.lsn = node.journal_vclock[origin] + 1,
              "Rows of one origin arrive strictly in order")
    THEN SetJournal(ArrAppend(entry, node.journal),
         SetJournalVclock(VclockSet(node.journal_vclock, origin, entry.lsn),
         SetValidAcks(valid_acks,
         node)))
    ELSE node

\* Check if all journal entries from 'from' node are present in 'to' node.
JournalIsFullyReplicatedTo(from, to) ==
    VclockGE(to.journal_vclock, from.journal_vclock)

--------------------------------------------------------------------------------
\*
\* Transactions
\*

\* Create a new transaction entry. It needs no term - the receivers derive it
\* from the origin's component in their limbo term map.
EntryNewTransaction(origin_id, lsn, data) == [
    type |-> EntryTypeTransaction,
    origin_id |-> origin_id,
    lsn |-> lsn,
    data |-> data
]

\* Check if the given object is a real row storing a transaction. Every real
\* row has a type, the empty placeholder has none.
TxnIsPresent(txn) ==
    /\ "type" \in DOMAIN txn
    /\ Assert(txn.type = EntryTypeTransaction,
              "A row in a transaction's place must be a transaction")

\* Create an empty transaction entry.
TxnEmpty == [null |-> NULL]

\* Transaction fate tracking. Each node decides for itself, exactly once per
\* transaction: a commit appends it to the data, a rollback puts it into the
\* rejected set. Two nodes can disagree only when the replication between
\* them is already broken. Otherwise it would mean a silent undetected fork,
\* one of the most dangerous outcomes.
TxnIsDecided(txn_data, node) ==
    \/ ArrContains(txn_data, node.data)
    \/ txn_data \in node.data_rejected

TxnCommit(txn_data, node) ==
    IF Assert(~TxnIsDecided(txn_data, node), "A transaction is decided once")
    THEN SetData(ArrAppend(txn_data, node.data), node)
    ELSE node

TxnReject(txn_data, node) ==
    IF Assert(~TxnIsDecided(txn_data, node), "A transaction is decided once")
    THEN SetDataRejected(node.data_rejected \union {txn_data}, node)
    ELSE node

--------------------------------------------------------------------------------
\*
\* PROMOTE entry
\*

\* Create a new PROMOTE entry.
\*
\* - The VRaft election term is the one where the node won the elections. The
\*   latest term known to this node.
\*
\* - The previous owner is the node which owned the limbo at the start of the
\*   promote chain.
\*
\* - Confirm LSN is the inclusive border until which the pending transactions
\*   must be confirmed.
\*
\* - Confirmed vclock carries the combined updates of the entire chain of the
\*   PROMOTEs. The first PROMOTE in the chain calculates it from its own state,
\*   and the next PROMOTEs simply bump their own LSNs there at positions
\*   corresponding to their origins.
\*
\* - Term map carries the combined term bumps of the entire chain of the
\*   PROMOTEs. So the VRaft election term of each PROMOTE bumps its origin's
\*   component in this map.
\*
\* The data the entry carries allows it on apply to update the node's state in
\* exactly the same way as if the entire chain of the PROMOTEs would be applied
\* one by one entry.
\*
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

\* Check if the given object is a real row storing a PROMOTE. Every real row
\* has a type, the empty placeholder has none.
PromoteIsPresent(promote) ==
    /\ "type" \in DOMAIN promote
    /\ Assert(promote.type = EntryTypePromote,
              "A row in a PROMOTE's place must be a PROMOTE")

\* Create an empty promotion entry.
PromoteEmpty == [null |-> NULL]

--------------------------------------------------------------------------------
\*
\* Collection of pending PROMOTE entries
\*

\* A map of pending promotion slots, one slot per node. Each node stores such
\* map to keep track what is the latest pending promotion from every other node.
\* To apply them on a valid CONFIRM, and to notice and reject a CONFIRM
\* attempting to apply a poisoned PROMOTE. But not all nodes have a pending
\* PROMOTE all the time, so the map's slots are usually empty.
PromotionsDictNew == [i \in NodeIDs |-> PromoteEmpty]

PromotionsSet(nid, promote, promotions) ==
    \* A node accepts maximum one pending PROMOTE per term. Others are rejected
    \* as a split-brain before this action happens.
    IF Assert(\A other \in NodeIDs \ {nid}:
                  ~PromoteIsPresent(promotions[other]) \/
                  promotions[other].raft_term # promote.raft_term,
              "No two pending promotions share a term")
    THEN [promotions EXCEPT ![nid] = promote]
    ELSE promotions

PromotionsFindPending(nid, lsn, promotions) ==
    LET promote == promotions[nid]
    IN IF PromoteIsPresent(promote) /\ promote.lsn = lsn
       THEN promote
       ELSE PromoteEmpty

\* Drop the pending promotions covered by a newer applied one. This means
\* dropping all such pending promotions that their term is covered by the
\* committed term map.
\*
\* The logic is simple. If a pending PROMOTE P1 from node N1 has term T1, and a
\* just committed PROMOTE P2 had term_map[N1] >= T1, it means P2 had in its
\* chain a PROMOTE from N1: either P1 exactly or an even newer one. Either way
\* P2 carried a decision from N1 which is at least as new as P1. And N1 couldn't
\* emit anything else, because otherwise it would have been replicated before
\* P2. This means P1 is covered by P2 and can be just dropped.
\*
\* But if there is a pending PROMOTE P3 from N3 with term T3 such as P2's
\* term_map[N3] < T3, then P3 still remains. For its own CONFIRM later and for
\* poison tracking. It, or anything newer from N3, clearly wasn't in the chain
\* led by P2.
PromotionsRemoveCovered(term_map, promotions) ==
    [nid \in DOMAIN(promotions) |->
        IF /\ PromoteIsPresent(promotions[nid])
           /\ promotions[nid].raft_term <= term_map[nid]
        THEN PromoteEmpty
        ELSE promotions[nid]]

--------------------------------------------------------------------------------
\*
\* Generic helpers
\*

\* Create a new CONFIRM entry to confirm the origin's rows up to the provided
\* confirm LSN. The rows include all what the origin sent until now -
\* transactions and PROMOTEs, which are essentially also transactions.
EntryNewConfirm(origin_id, lsn, confirm_lsn) == [
    type |-> EntryTypeConfirm,
    origin_id |-> origin_id,
    lsn |-> lsn,
    confirm_lsn |-> confirm_lsn
]

\* Check if node has the entry in its journal.
HasEntry(node, entry) ==
    entry.lsn <= node.journal_vclock[entry.origin_id]

\* See whether src still can directly send data to dst.
LinkIsOpenFromTo(src, dst) == src \in Nodes[dst].appliers

CountAcksForEntry(entry) ==
    LET origin == entry.origin_id
    IN
    \* The logic of establishing which acks are valid and counted is explained
    \* where they get set. The only untrivial thing to not forget here is to
    \* check that the link from the acker to the origin exists. Otherwise even
    \* if an ack is valid, it can't be 'seen' by the origin anyway without a
    \* link.
    IF Assert(origin \in Nodes[origin].valid_acks,
              "The origin's own write of the entry is an ack")
    THEN Cardinality({nid \in NodeIDs:
             /\ HasEntry(Nodes[nid], entry)
             /\ origin \in Nodes[nid].valid_acks
             /\ nid = origin \/ LinkIsOpenFromTo(origin, nid)})
    ELSE 0

\* Node constructor.
NodeNew(nid) == [
    role |-> IF nid \in VoterIDs THEN NodeRoleVoter ELSE NodeRoleCandidate,
    \* The nodes this one receives from. A voter receives no rows, but its
    \* links still carry the terms and the vote requests.
    appliers |-> NodeIDs \ {nid},
    journal |-> <<>>,
    raft_term |-> 1,
    raft_state |-> ElectionStateFollower,
    \* The node voted for in the current term, NULL when didn't vote yet. The
    \* votes are not globally visible journal entries - they are VRaft messages,
    \* exchanged explicitly by the peers over the replication links. Local
    \* storage of the votes is needed, but its details are irrelevant (might be
    \* stored in the journal as non-replicated rows, for example).
    raft_vote |-> NULL,
    limbo_state |-> LimboStateReplica,
    limbo_term |-> 1,
    limbo_owner |-> InitialLimboOwner,
    limbo_vclock |-> [i \in NodeIDs |-> -1],
    \* Last known term of each node, taken from their last confirmed PROMOTEs.
    \* It helps to see which row arrived in which term from every origin, and
    \* this in turn helps with filtering and rollbacks.
    limbo_term_map |-> [i \in NodeIDs |-> 0],
    \* Pending promotions. Empty at first.
    limbo_promotions |-> PromotionsDictNew,
    \* The limbo has at most one transaction, for simplicity. It still allows to
    \* test the transaction's rollback, commit, conflicts, and reorders. Scales
    \* to multiple transactions by induction, while the single transaction helps
    \* to keep the validation state space small.
    limbo |-> TxnEmpty,
    \* Ordered list of committed transactions, in the commit order. The order is
    \* important - for example, a couple of transactions can be assigning the
    \* same key, and the result of the newest transaction must be on top them.
    data |-> <<>>,
    \* The rolled back transactions. Their order is irrelevant. It can't be
    \* global the same, and doesn't need to be. The reason is simple - rolled
    \* back txns were never visible in the first place and couldn't have
    \* affected the committed state of the node (which in turn is entirely
    \* expressed by the ordered list of committed transactions).
    data_rejected |-> {},
    \* The count of the journal rows of each origin.
    journal_vclock |-> [i \in NodeIDs |-> 0],
    \* The origins whose latest row this node acked with a term not above
    \* the row's own. Read more in the actions setting the acks.
    valid_acks |-> {},
    \* Whether the node created a transaction since it was elected. A node
    \* creates at most one transaction per term - a state space reduction.
    made_txn |-> FALSE
]

--------------------------------------------------------------------------------
\*
\* Elections
\*
\* The model of elections is entirely take from the original Raft. It builds
\* upon the assumption that vanilla Raft election and voting are proven to be
\* correct. This allows to simplify the elections a bit a not re-verify states
\* which don't matter for synchronous replication, like ignorance of outdated
\* votes.
\*

\* Whether the node has a PROMOTE of the given term from another origin - a
\* second leader elected in the past with the same term.
NodeHasForeignPromoteOfTerm(node, term, origin) ==
    LET journal == node.journal
    IN
    /\ \E i \in DOMAIN(journal):
       /\ journal[i].type = EntryTypePromote
       /\ journal[i].raft_term = term
       /\ journal[i].origin_id # origin

\* Node can occasionally bump its term to start an election. Then it votes for
\* itself and becomes a candidate. In reality this simulates node restarts (on
\* restart it loses leadership and might decide to re-elect itself) or seeing
\* no leader for a while (and deciding to take its place). Or even an explicit
\* manual node promotion.
NodeBumpTerm(nid) ==
    LET node == Nodes[nid]
    IN
    /\ node.raft_term < MaxTerm
    /\ node.role = NodeRoleCandidate
    \* ---
    /\ Nodes' = NodesUpdate(nid,
                SetRaftTerm(node.raft_term + 1,
                SetRaftVote(nid,
                SetRaftState(ElectionStateCandidate,
                SetLimboState(LimboStateReplica,
                node)))))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

\* Node votes for a candidate of its term, once per term. The candidate's
\* journal must contain everything the voter has. The vote request is a
\* VRaft message coming over the candidate's replication link.
NodeVote(voter_nid, cand_nid) ==
    LET voter == Nodes[voter_nid]
        cand == Nodes[cand_nid]
    IN
    \* The 1-quorum case needs no votes - candidates immediately win their own
    \* election on term bump and need no external votes. This helps to reduce
    \* the verification state space. In reality nodes still can receive new
    \* votes after the got the quorum already, but it is identical in the
    \* outcome to the excessive votes being made and not delivered, or not made
    \* at all. The vote made by some node but not delivered in the model
    \* fullscan is covered by that node simply not voting. The outcome is the
    \* same.
    /\ Quorum > 1
    \* CHECK: this doesn't seem needed - vote == NULL covers this already, since
    \* the candidates fill it out immediately when become candidates. Or can
    \* make this an Assert below the vote NULL check.
    /\ voter_nid # cand_nid
    /\ voter.raft_term = cand.raft_term
    /\ voter.raft_vote = NULL
    \* In reality the voter will not have a certain knowledge of the candidate
    \* being still actively collecting votes. Worst case - the voter votes and
    \* the former candidate will just ignore the vote. Such late votes make no
    \* difference for any states whatsoever. Omitting them helps to reduce the
    \* model verification state space.
    /\ cand.raft_state = ElectionStateCandidate
    /\ Assert(cand.raft_vote = cand_nid, "A candidate has voted for itself")
    /\ JournalIsFullyReplicatedTo(voter, cand)
    \* Votes aren't delivered transitively. Need a direct connection to be able
    \* to vote.
    /\ LinkIsOpenFromTo(cand_nid, voter_nid)
    \* ---
    /\ Nodes' = NodesUpdate(voter_nid, SetRaftVote(cand_nid, voter))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

\* Node becomes VRaft leader after gathering a quorum of votes.
NodeBecomeLeader(nid) ==
    LET node == Nodes[nid]
        term == node.raft_term
    IN
    /\ node.raft_state = ElectionStateCandidate
    /\ Assert(node.role = NodeRoleCandidate, "A voter is never a candidate")
    /\ Assert(node.raft_vote = nid, "A candidate has voted for itself")
    /\ Cardinality({other \in NodeIDs:
            /\ Nodes[other].raft_term = term
            /\ Nodes[other].raft_vote = nid
            /\ other = nid \/ LinkIsOpenFromTo(other, nid)}) >= Quorum
    \* ---
    /\ Nodes' = NodesUpdate(nid,
                SetRaftState(ElectionStateLeader,
                SetMadeTxn(FALSE,
                node)))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

\* Node observes a higher term from another node and steps down. In reality can
\* be from an ack, from a heartbeat, from a reconnect.
NodeObserveHigherTerm(nid) ==
    LET node == Nodes[nid]
    IN
    \* Cheap check first: the sets below are built only when there is a
    \* higher term somewhere at all. This saves CPU work during model
    \* verification.
    \*
    \* Note how \E is avoided and replaced with ~\A - the reason is that TLC
    \* branches states on all possible outcomes of \E. Here is would be
    \* pointless since the result is the same no matter which node or nodes had
    \* a higher term.
    /\ ~\A other \in NodeIDs: Nodes[other].raft_term <= node.raft_term
    /\ LET peers == {other \in NodeIDs \ {nid}:
                        LinkIsOpenFromTo(other, nid) \/ LinkIsOpenFromTo(nid, other)}
           terms == {Nodes[other].raft_term : other \in peers}
       IN
       \* The \E selection is done over directly visible terms, not nodes.
       \* Because it doesn't matter from which node a new term was observed.
       \* This helps to reduce the model state space.
       \E term \in terms:
        /\ term > node.raft_term
        \* ---
        /\ Nodes' = NodesUpdate(nid,
                    SetRaftTerm(term,
                    SetRaftVote(NULL,
                    SetRaftState(ElectionStateFollower,
                    SetLimboState(LimboStateReplica,
                    node)))))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

--------------------------------------------------------------------------------
\*
\* Promotion
\*
\* A newly elected leader executes a transaction which claims the limbo
\* ownership for this leader for its current electoral term.
\*

\* A pending PROMOTE is poisoned when no CONFIRM can apply it on this node. It
\* means the PROMOTE would try to put this node into the past: revert some
\* confirmed LSN from any past leaders or revert a confirmed term of an older
\* PROMOTION.
\*
\* PROMOTE can get poisoned when some decisions were made without including the
\* origin of this PROMOTE into their quorums.
\*
\* Poison isn't a sign of a split-brain. It appears in any cluster - for
\* example, when a leader loses the leadership before its PROMOTE reaches
\* anybody.
\*
\* The main point is that with a majority quorum it never gets confirmed. This
\* would require the author of this PROMOTE to cross its path with at least one
\* node which saw the decisions made by the other nodes, and that would make the
\* node observe its outdated state, which in turn makes it abandon the poisoned
\* PROMOTE.
\*
\* With <= 50% quorum the author can get elected and confirm its PROMOTE before
\* it realizes that it is poisoned. This would be an actual split-brain.
NodeSeesPromotePoisoned(node, promote) ==
    \/ promote.raft_term <= node.limbo_term
    \/ ~VclockGE(promote.term_map, node.limbo_term_map)
    \/ ~VclockGE(promote.confirmed_vclock, node.limbo_vclock)

NodeCanConfirmPromote(node, promote) == ~NodeSeesPromotePoisoned(node, promote)

\* The filter of a PROMOTE entering the journal, own or received.
\*
\* The filter isn't just to reject poisoned PROMOTEs. They not necessarily will
\* be confirmed and can appear during normal cluster work.
\*
\* The filter is trying to catch problems which 100% indicate a split-brain
\* regardless of whether the PROMOTE will be confirmed in the future or not.
\*
\* For example, if the PROMOTE's term is seen as the latest seen term of any
\* of the nodes, it means 2 nodes won elections in a single term. That is
\* undoubtedly a split-brain.
\*
\* Allowing such PROMOTE into the journal would break the limbo's state as it
\* would have more than one pending PROMOTE on the same term.
\*
\* At the same time, the PROMOTE whose term is not equal to any of the currently
\* visible terms, then there is no way to tell a split-brain. And allowing such
\* PROMOTE in will break no invariants yet.
NodeCanAcceptPromote(node, term, origin) ==
    \A nid \in NodeIDs \ {origin}:
        /\ node.limbo_term_map[nid] # term
        /\ PromoteIsPresent(node.limbo_promotions[nid]) =>
           node.limbo_promotions[nid].raft_term # term

\* The latest pending PROMOTE to chain from. The criteria is that it must not be
\* poisoned as that would make the new PROMOTE inherit the poison and it will
\* never be able to get confirmed.
NodeFindTopOfPromoteChain(node) ==
    LET promotions == node.limbo_promotions
        live == {nid \in DOMAIN(promotions):
                    /\ PromoteIsPresent(promotions[nid])
                    /\ ~NodeSeesPromotePoisoned(node, promotions[nid])}
    IN IF live = {}
       THEN PromoteEmpty
       ELSE promotions[CHOOSE nid \in live:
                \A other \in live:
                    promotions[nid].raft_term >= promotions[other].raft_term]

\* Save the PROMOTE into the local journal.
NodeWritePromote(entry, node) ==
    LET origin == entry.origin_id
        old_promote == node.limbo_promotions[origin]
    IN
    IF /\ Assert(NodeCanAcceptPromote(node, entry.raft_term, origin),
                 "Only a PROMOTE passing the filter is stored")
       /\ Assert(~PromoteIsPresent(old_promote) \/ old_promote.raft_term < entry.raft_term,
                 "New PROMOTE must have term > old PROMOTE term")
       /\ Assert(~PromoteIsPresent(old_promote) \/ old_promote.lsn < entry.lsn,
                 "New PROMOTE must have LSN > old PROMOTE LSN")
       /\ Assert(~IsMajorityQuorum \/
                 ~NodeHasForeignPromoteOfTerm(node, entry.raft_term, origin),
                 "A majority quorum elects one leader per term")
    THEN SetLimboPromotions(
         PromotionsSet(origin, entry, node.limbo_promotions),
         JournalAppend(entry, node))
    ELSE node

\* Apply a pending PROMOTE by its CONFIRM.
NodeApplyPromote(promote, confirm_entry, node) ==
    LET has_txn == TxnIsPresent(node.limbo)
        txn_entry == node.limbo
        \* The owner of the txn can be different from what the PROMOTE sees as
        \* the previous owner. This can happen when there was a chain
        \*
        \*   queue{owner:N1}
        \*       -> PROMOTE P1{origin:N2, owner:N1}
        \*       -> PROMOTE P2{origin: N3, owner: N1}
        \*
        \* The PROMOTE P2 chained from P1. If P1 gets confirmed on its own
        \* before P2, then the queue becomes owned by N2, while P2 sees it still
        \* as N1. It is exactly what is expected and is totally fine.
        owner_matches == has_txn /\ txn_entry.origin_id = promote.prev_owner
        should_commit == owner_matches /\ txn_entry.lsn <= promote.confirm_lsn
        decided_node == IF ~has_txn THEN node
                        ELSE IF should_commit THEN TxnCommit(txn_entry.data, node)
                        ELSE TxnReject(txn_entry.data, node)
    IN
    IF /\ Assert(NodeCanConfirmPromote(node, promote),
                 "Only a PROMOTE passing the filter is applied")
       /\ Assert(has_txn => txn_entry.origin_id = node.limbo_owner,
                 "Transaction origin must match current limbo owner")
       \* CHECK: replace this with ~\A to prevent state blowup.
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

\* The node won elections and now wants to get PROMOTEd. The node can do so right away,
\* even if there are pending transactions. The PROMOTE anyway will need to gather the
\* quorum. If it succeeds, then it will implicitly collect the quorum for the pending
\* transactions too and will CONFIRM them alongside. If it fails to get quorum, then no
\* harm done, nothing was committed or rolled back.
NodeStartPromote(nid) ==
    LET node == Nodes[nid]
    IN
    /\ PromotesWritten < MaxPromotes
    \* Only the actual leader can begin its promotion.
    /\ node.raft_state = ElectionStateLeader
    \* And its limbo must not be already promoted.
    /\ node.raft_term > node.limbo_term
    \* A node can receive PROMOTE with a term higher than the latest known
    \* election term. It is possible, because the protocol allows to send
    \* election messages and limbo messages via separate channels, and they can
    \* race each other. When it is observed, the node's time of leadership is
    \* counted, it will be demoted soon. In theory starting an own PROMOTE right
    \* now would not break anything, but this PROMOTE will not win either, so
    \* might as well not even try.
    /\ \A other \in NodeIDs:
        PromoteIsPresent(node.limbo_promotions[other]) =>
        node.limbo_promotions[other].raft_term < node.raft_term
    \* The own PROMOTE goes through the same filter as the received ones.
    /\ NodeCanAcceptPromote(node, node.raft_term, nid)
    \* ---
    \* The limbo queue of pending transactions might look in general like this:
    \*
    \*     txn_1 -> ... -> txn_N -> PROMOTE_1 -> ... -> PROMOTE_N
    \*
    \* Even though the PROMOTEs can be stored separately from the transactions,
    \* the queue logically is one for them all.
    \*
    \* Adding a new PROMOTE means adding it in the end of the queue. If there is
    \* an existing PROMOTE_N, then the new one chains on top of it. Given that
    \* PROMOTE chains are squashed into one cumulative PROMOTE, the state is
    \* more like
    \*
    \*     txn_1 -> ... -> txn_N -> PROMOTE{1...N}
    \*
    \* The job is just to copy it and squash the new one into it to build this:
    \* PROMOTE{1...N+1}.
    \*
    \* Otherwise if there are no pending PROMOTEs, then the new one is built as
    \* the first one in the chain, simply on top of the queue state.
    /\ LET latest_promote == NodeFindTopOfPromoteChain(node)
           has_pending == PromoteIsPresent(latest_promote)
           \* CHECK: I need an quick example here why we must not always take
           \* the node.limbo_owner.
           prev_owner == IF has_pending
                         THEN latest_promote.prev_owner
                         ELSE node.limbo_owner
           \* CHECK: I need an quick example here why we must not always take
           \* the LSN of the limbo txn.
           confirm_lsn == IF has_pending
                          THEN latest_promote.confirm_lsn
                          ELSE IF TxnIsPresent(node.limbo)
                               THEN node.limbo.lsn
                               ELSE node.limbo_vclock[node.limbo_owner]
           \* CHECK: I need an quick example here why we must not always take
           \* the limbo vclock + confirm LSN.
           base_vclock == IF has_pending
                          THEN latest_promote.confirmed_vclock
                          ELSE VclockSet(node.limbo_vclock, prev_owner, confirm_lsn)
           \* The term map stores terms of all the PROMOTEs squashed in the top
           \* PROMOTE. When there is one - take it + simply add the new one into
           \* it.
           base_term_map == IF has_pending
                            THEN latest_promote.term_map
                            ELSE node.limbo_term_map
           lsn == JournalNextLSN(nid, node)
           \* The row itself is in its confirmed vclock: its CONFIRM names
           \* it, and a chain over it carries it on, so a late CONFIRM of a
           \* covered PROMOTE is recognized as already done.
           entry == EntryNewPromote(
               nid,
               lsn,
               node.raft_term,
               prev_owner,
               confirm_lsn,
               \* The PROMOTE itself is also in the confirmed vclock of its own
               \* origin. This, on confirm, will keep the PROMOTE's LSN covered
               \* by the confirmed vclock.
               \* CHECK: what if it is not added? An example.
               VclockSet(base_vclock, nid, lsn),
               VclockSet(base_term_map, nid, node.raft_term)
           )
       IN Nodes' = NodesUpdate(nid, NodeWritePromote(entry, node))
    /\ PromotesWritten' = PromotesWritten + 1
    /\ UNCHANGED TransactionsToDo

------------
------------ TODO

\* Leader confirms PROMOTE after quorum receives it. The own CONFIRM goes
\* through the same filter as the received ones - it is never written when it
\* would be refused right here.
NodeConfirmPromote(nid) ==
    LET node == Nodes[nid]
        promote_entry == node.limbo_promotions[nid]
    IN
    /\ node.raft_state = ElectionStateLeader
    /\ PromoteIsPresent(promote_entry)
    /\ promote_entry.raft_term = node.raft_term
    \* The own PROMOTE poisoned by a newer applied promotion is the leader
    \* overtaken before observing the term. By an older one - a fork.
    /\ Assert(\/ ~IsMajorityQuorum
              \/ node.limbo_term > promote_entry.raft_term
              \/ NodeCanConfirmPromote(node, promote_entry),
              "A majority quorum never confirms behind the leader's back")
    /\ NodeCanConfirmPromote(node, promote_entry)
    /\ CountAcksForEntry(promote_entry) >= Quorum
    \* ---
    /\ Assert(promote_entry.raft_term > node.limbo_term,
              "Local pending promote's term is always bigger than the last confirmed limbo term")
    /\ LET confirm_entry == EntryNewConfirm(
               nid,
               JournalNextLSN(nid, node),
               promote_entry.lsn
           )
       IN Nodes' = NodesUpdate(nid,
                   SetLimboState(LimboStateLeader,
                   NodeApplyPromote(promote_entry, confirm_entry, node)))
    /\ UNCHANGED <<TransactionsToDo, PromotesWritten>>

--------------------------------------------------------------------------------
\*
\* Limbo transaction actions
\*

\* Limbo leader creates a new transaction
NodeCreateTransaction(nid) ==
    LET node == Nodes[nid]
    IN
    /\ node.limbo_state = LimboStateLeader
    /\ ~TxnIsPresent(node.limbo)
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
           entry == EntryNewTransaction(nid, JournalNextLSN(nid, node), txn_data)
       IN
        /\ TransactionsToDo' = TransactionsToDo \ {txn_data}
        /\ UNCHANGED PromotesWritten
        /\ Nodes' = NodesUpdate(nid,
                    SetLimbo(entry,
                    SetMadeTxn(TRUE,
                    JournalAppend(entry, node))))

\* Limbo leader confirms transaction after quorum receives it
NodeConfirmTransaction(nid) ==
    LET node == Nodes[nid]
        txn_entry == node.limbo
    IN
    /\ node.limbo_state = LimboStateLeader
    /\ TxnIsPresent(node.limbo)
    /\ CountAcksForEntry(txn_entry) >= Quorum
    \* ---
    /\ LET confirm_entry == EntryNewConfirm(
               nid,
               JournalNextLSN(nid, node),
               txn_entry.lsn
           )
       IN
       /\ Assert(txn_entry.lsn >= node.limbo_vclock[nid],
                 "Vclock LSN must not decrease")
       /\ Nodes' = NodesUpdate(nid,
                    SetLimbo(TxnEmpty,
                    SetLimboVclock(VclockSet(node.limbo_vclock, nid, txn_entry.lsn),
                    JournalAppend(confirm_entry,
                    TxnCommit(txn_entry.data, node)))))
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
    IF ~NodeCanAcceptPromote(dst_node, entry.raft_term, origin)
    THEN LinkCloseFromAnyOfTo(srcs, dst_nid)
    ELSE
        \* A chain carrying this PROMOTE's term is always delivered after it,
        \* so it is never covered on arrival.
        /\ Assert(entry.raft_term > dst_node.limbo_term_map[origin],
                  "PROMOTE can't be covered on arrival")
        /\ Nodes' = NodesUpdate(dst_nid, NodeWritePromote(entry, dst_node))

\* Apply CONFIRM on PROMOTE entry to destination node. The CONFIRM of a
\* PROMOTE not passing the filter is a fork - its author confirmed it without
\* knowing something applied here.
ReplicateConfirmPromote(entry, srcs, dst_nid, promote) ==
    LET dst_node == Nodes[dst_nid]
    IN
    /\ Assert(entry.confirm_lsn = promote.lsn,
             "The CONFIRM names the PROMOTE's own row")
    /\ IF ~NodeCanConfirmPromote(dst_node, promote)
       THEN LinkCloseFromAnyOfTo(srcs, dst_nid)
       ELSE
           \* The ownership went to the PROMOTE's origin. A limbo leader
           \* which confirmed its own PROMOTE with this newer one pending
           \* is a replica again.
           LET new_node == SetLimboState(LimboStateReplica,
                           NodeApplyPromote(promote, entry, dst_node))
               own_promote == new_node.limbo_promotions[dst_nid]
           IN
           \* A leader's own pending PROMOTE stays confirmable through an
           \* older promotion applied beside it - a majority confirms
           \* nothing behind its back. A newer one is a different thing: the
           \* leader got overtaken, it just hasn't observed the term yet,
           \* the term can travel behind the PROMOTE.
           /\ Assert(\/ ~IsMajorityQuorum
                     \/ new_node.raft_state # ElectionStateLeader
                     \/ ~PromoteIsPresent(own_promote)
                     \/ promote.raft_term > own_promote.raft_term
                     \/ NodeCanConfirmPromote(new_node, own_promote),
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
            /\ TxnIsPresent(txn_entry)
            /\ txn_entry.lsn <= entry.confirm_lsn
    IN
    \* The rows of one origin arrive in order, and a PROMOTE's CONFIRM comes
    \* before anything written after the PROMOTE. So while the origin has a
    \* PROMOTE pending here, the only CONFIRM of its that can arrive is that
    \* PROMOTE's own.
    /\ Assert(~PromoteIsPresent(pending) \/ pending.lsn = entry.confirm_lsn,
              "A CONFIRM from an origin with a pending PROMOTE is that PROMOTE's")
    /\ IF PromoteIsPresent(promote)
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
                         TxnCommit(txn_entry.data, dst_node)))))
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
        /\ Assert(~TxnIsPresent(dst_node.limbo),
                  "The owner's transactions are queued one at a time")
        /\ Nodes' = NodesUpdate(dst_nid,
                     SetLimbo(entry,
                     JournalAppend(entry, dst_node)))
    ELSE IF is_old_term
    THEN
        \* Rollback old term transaction
        Nodes' = NodesUpdate(dst_nid,
                  JournalAppend(entry,
                  TxnReject(entry.data, dst_node)))
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

Init ==
    /\ Nodes = [nid \in NodeIDs |-> NodeNew(nid)]
    /\ TransactionsToDo = AllTransactions
    /\ PromotesWritten = 0

Next ==
    \/ \E nid \in NodeIDs: NodeBumpTerm(nid)
    \/ \E nid \in NodeIDs, cand \in NodeIDs: NodeVote(nid, cand)
    \/ \E nid \in NodeIDs: NodeBecomeLeader(nid)
    \/ \E nid \in NodeIDs: NodeObserveHigherTerm(nid)
    \/ \E nid \in NodeIDs: NodeStartPromote(nid)
    \/ \E nid \in NodeIDs: NodeConfirmPromote(nid)
    \/ \E nid \in NodeIDs: NodeCreateTransaction(nid)
    \/ \E nid \in NodeIDs: NodeConfirmTransaction(nid)
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
            /\ node.raft_state = ElectionStateLeader
            /\ node.limbo_owner = nid
            /\ node.limbo_term = node.raft_term

\* If limbo has a transaction, its origin must match limbo owner
LimboOwnerInvariant ==
    \A nid \in NodeIDs:
        LET node == Nodes[nid]
        IN TxnIsPresent(node.limbo) =>
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
