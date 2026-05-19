------------------------- MODULE ForwardRaftWitness ---------------------------
\*
\* Reachability witnesses for ForwardRaft. Each operator is the negation of a
\* scenario the model is expected to reach. Checked as an invariant, it makes
\* TLC stop with the shortest trace leading to the scenario - the proof the
\* state space covers it, and a script for a test. A run which passes means
\* the scenario is unreachable, which is a finding of its own.
\*
\* A scenario visible in a single state is an INVARIANT. One visible only in
\* a step - when the state after it is reachable another way too - is a
\* PROPERTY of the form [][~step]_vars, which TLC checks on every transition
\* of the same search.
\*
\* Run one at a time: tlc -config witness/<Name>.cfg ForwardRaftWitness.tla
\*

EXTENDS ForwardRaft

\* Every data node refuses every other one. The voters stay in the appliers -
\* they send no rows, so there is nothing to refuse from them.
WitnessAllLinksBroken ==
    ~\A nid \in DataNodes: Nodes[nid].appliers \cap DataNodes = {}

\* Two leaders elected in the same term, each with its own PROMOTE.
WitnessTwoLeadersOneTerm ==
    ~\E a \in DataNodes, b \in DataNodes:
        /\ a # b
        /\ PromoteIsValid(Nodes[a].limbo_promotions[a])
        /\ PromoteIsValid(Nodes[b].limbo_promotions[b])
        /\ Nodes[a].limbo_promotions[a].raft_term =
           Nodes[b].limbo_promotions[b].raft_term

\* A fork materialized in data: a transaction committed on one node and
\* rolled back on another.
WitnessDataFork ==
    ~\E a \in DataNodes, b \in DataNodes, t \in AllTransactions:
        /\ t \in Nodes[a].data_rejected
        /\ ArrContains(t, Nodes[b].data)

\* The CONFIRM of a poisoned pending PROMOTE arrives and gets refused. The
\* refusal is the step closing the link, and the row refused is the first one
\* of the source's journal missing on the destination. The CONFIRM can come
\* over any link, not only the origin's, and a closed link alone doesn't say
\* which row closed it - hence a step property.
WitnessPoisonedConfirmRefused ==
    [][~\E src \in DataNodes, dst \in DataNodes:
          /\ src \in Nodes[dst].appliers
          /\ src \notin Nodes'[dst].appliers
          /\ LET src_node == Nodes[src]
                 dst_node == Nodes[dst]
                 entry == src_node.journal[NextEntryToReplicate(src_node, dst_node)]
                 promote == PromotionsFindPending(entry.origin_id, entry.confirm_lsn,
                                                  dst_node.limbo_promotions)
             IN
             /\ entry.type = EntryTypeConfirm
             /\ PromoteIsValid(promote)
             /\ PromoteIsPoisoned(dst_node, promote)]_vars

\* A PROMOTE delivered ahead of its term - the receiver's Raft term is still
\* below the pending PROMOTE's one.
WitnessPromoteAheadOfTerm ==
    ~\E nid \in DataNodes, origin \in DataNodes:
        LET promote == Nodes[nid].limbo_promotions[origin]
        IN
        /\ PromoteIsValid(promote)
        /\ promote.raft_term > Nodes[nid].raft_term

\* A confirmed chained PROMOTE which mattered: its CONFIRM advances the
\* applied term map in two components at once, bringing a term the node never
\* applied. A PROMOTE written over an applied limbo advances exactly one - the
\* CONFIRMs are delivered in journal order and the applied map only moves
\* forward, so all its other components are applied before it. The state
\* after the step is reachable by sequential promotions too, hence a step
\* property.
WitnessChainConfirmed ==
    [][~\E nid \in DataNodes:
          Cardinality({o \in NodeIDs:
              Nodes'[nid].limbo_term_map[o] > Nodes[nid].limbo_term_map[o]})
          >= 2]_vars

\* A leader confirms its own PROMOTE while a newer one is pending beside it
\* and stays live - the older-pending-confirmed-later case. Live after the
\* step means the newer one's map carries the term just applied, so its author
\* knew this PROMOTE, and could know it only as pending: applied would mean
\* this node confirmed it earlier, not in this step. The newer one is chained
\* from it and applies afterwards.
WitnessOlderPromoteConfirmedLater ==
    [][~\E nid \in DataNodes:
          LET node == Nodes'[nid]
          IN
          /\ node.limbo_owner = nid
          /\ Nodes[nid].limbo_term < node.limbo_term
          /\ \E o \in NodeIDs \ {nid}:
              /\ PromoteIsValid(node.limbo_promotions[o])
              /\ node.limbo_promotions[o].raft_term > node.limbo_term
              /\ ~PromoteIsPoisoned(node, node.limbo_promotions[o])]_vars

\* A limbo leader with a newer PROMOTE pending beside it. The pending demotes
\* nothing - the leader keeps writing until it observes the Raft term or
\* applies the CONFIRM, as the code derives the limbo state. The newer PROMOTE
\* reached the leader ahead of its term.
WitnessLeaderWithNewerPending ==
    ~\E nid \in DataNodes, origin \in NodeIDs:
        LET node == Nodes[nid]
            promote == node.limbo_promotions[origin]
        IN
        /\ nid # origin
        /\ node.limbo_state = LimboStateLeader
        /\ PromoteIsValid(promote)
        /\ promote.raft_term > node.limbo_term

\* A CONFIRM written on a delayed ack: among the counted acks is one from a
\* node which has moved to a higher term since it appended the row, and
\* without such acks the quorum is not there. The ack carried the term of the
\* append, so the origin counts it. Needs a quorum above one - the own ack
\* is never stale - so this one runs on a majority config.
WitnessConfirmOnDelayedAck ==
    [][~\E nid \in DataNodes:
          LET node == Nodes[nid]
              FreshAcksBelowQuorum(entry) ==
                  Cardinality({o \in NodeIDs:
                      /\ HasEntry(Nodes[o], entry)
                      /\ Nodes[o].raft_term <= node.raft_term}) < Quorum
          IN
          \/ LimboConfirmPromote(nid) /\ FreshAcksBelowQuorum(node.limbo_promotions[nid])
          \/ LimboConfirmTransaction(nid) /\ FreshAcksBelowQuorum(node.limbo)]_vars

\* The replication is finished with the cluster split in two groups: every
\* pair across the cut refused each other both ways, every pair inside a
\* group still exchanges rows both ways - and is consistent, by
\* JournalDeterminesStateInvariant. A one-way refusal is not a split: the
\* refused side still follows the other.
WitnessQuiescentSplit ==
    ~(/\ Quiescent
      /\ \E G \in SUBSET DataNodes:
          /\ G # {} /\ G # DataNodes
          /\ \A a \in G, b \in DataNodes \ G:
              a \notin Nodes[b].appliers /\ b \notin Nodes[a].appliers
          /\ \A a \in DataNodes, b \in DataNodes:
              a # b /\ (a \in G <=> b \in G) =>
                  a \in Nodes[b].appliers /\ b \in Nodes[a].appliers)

\* The three-promotion patterns, what a fourth term is for.

\* A chain through a chain, confirmed: the CONFIRM advances the applied term
\* map in three components at once - the last link brought two terms the node
\* never applied, on top of its own.
WitnessChainOfThree ==
    [][~\E nid \in DataNodes:
          Cardinality({o \in NodeIDs:
              Nodes'[nid].limbo_term_map[o] > Nodes[nid].limbo_term_map[o]})
          >= 3]_vars

\* A chain revisits its origin: a leader chains its new PROMOTE over another
\* node's pending one, which is itself above the leader's own, still live,
\* pending from an earlier term - the own slot is squashed to the new one.
WitnessChainSquashesOwnPending ==
    [][~\E nid \in DataNodes:
          LET node == Nodes[nid]
              own == node.limbo_promotions[nid]
          IN
          /\ LimboWritePromote(nid)
          /\ PromoteIsValid(own)
          /\ ~PromoteIsPoisoned(node, own)
          /\ \E other \in NodeIDs:
              LET pending == node.limbo_promotions[other]
              IN
              /\ other # nid
              /\ PromoteIsValid(pending)
              /\ ~PromoteIsPoisoned(node, pending)
              /\ pending.raft_term > own.raft_term]_vars

\* A poisoned pending PROMOTE replaced in its slot by its origin's later one:
\* the origin lost the term, saw the newer leadership, and promoted again.
WitnessPoisonedReplacedByOrigin ==
    [][~\E nid \in DataNodes, origin \in NodeIDs:
          LET old == Nodes[nid].limbo_promotions[origin]
              new == Nodes'[nid].limbo_promotions[origin]
          IN
          /\ PromoteIsValid(old)
          /\ PromoteIsPoisoned(Nodes[nid], old)
          /\ PromoteIsValid(new)
          /\ new.raft_term > old.raft_term]_vars

\* Three fork sides: every data node owns the limbo on itself, each having
\* confirmed its own PROMOTE alone and applied no one else's.
WitnessThreeOwners ==
    ~\A nid \in DataNodes:
        Nodes[nid].limbo_owner = nid /\ Nodes[nid].limbo_term > 1

================================================================================
