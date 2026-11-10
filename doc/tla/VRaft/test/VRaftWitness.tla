------------------------------ MODULE VRaftWitness -----------------------------
\*
\* Reachability witnesses for VRaft. A witness' job is to reach a specific
\* desired state on some spec and "stop" loud, signaling that the state is
\* reachable.
\*
\* It can be helpful to sanity-check that certain in-theory expected states of
\* the cluster are actually reached during the model tracing. Once reached, the
\* verification stops. Fullscan correctness checking is handled by other test
\* approaches.
\*
\* Witnessing also helps to confirm reachability of state transitions, from A to
\* B. Like state was A, and it becomes B on the next step.
\*

EXTENDS VRaft

\* Every data node refuses every other one. The voters stay connected -
\* they send no rows, so there is nothing to refuse from them. This is an
\* extreme case of split-brain when every node became its own independent part
\* of the cluster.
WitnessAllLinksBroken ==
    \A nid \in DataNodes: Nodes[nid].appliers \cap DataNodes = {}

\* Two leaders elected in the same term, each with its own PROMOTE.
WitnessTwoLeadersOneTerm ==
    \E a \in DataNodes, b \in DataNodes:
        /\ a # b
        /\ PromoteIsPresent(Nodes[a].limbo_promotions[a])
        /\ PromoteIsPresent(Nodes[b].limbo_promotions[b])
        /\ Nodes[a].limbo_promotions[a].term =
           Nodes[b].limbo_promotions[b].term

\* A fork materialized in data: a transaction committed on one node and rolled
\* back on another.
WitnessDataFork ==
    \E a \in DataNodes, b \in DataNodes, t \in AllTransactions:
        /\ t \in Nodes[a].data_rejected
        /\ ArrContains(t, Nodes[b].data)

\* The CONFIRM of a poisoned pending PROMOTE arrives and gets refused. The
\* refusal is the step closing the link, and the row refused is the first one
\* of the source's journal missing on the destination.
WitnessPoisonedConfirmRefused ==
    \E src \in DataNodes, dst \in DataNodes:
        \* The link src -> dst gets closed after the transition.
        /\ src \in Nodes[dst].appliers
        /\ src \notin Nodes'[dst].appliers
        \* The to-be-replicated-PROMOTE is seen as poisoned. Expecting to cause
        \* the link break on attempt to replicate it.
        /\ LET src_node == Nodes[src]
               dst_node == Nodes[dst]
               idx == NextEntryToReplicate(src_node, dst_node)
               entry == src_node.journal[idx]
               promote == PromotionsFindPending(
                    entry.origin_id, entry.confirm_lsn,
                    dst_node.limbo_promotions)
           IN
           /\ entry.type = EntryTypeConfirm
           /\ PromoteIsPresent(promote)
           /\ NodeSeesPromotePoisoned(dst_node, promote)

\* A PROMOTE delivered ahead of its election term. Possible because the election
\* and replication are allowed to go through different channels.
WitnessPromoteAheadOfTerm ==
    \E nid \in DataNodes, origin \in DataNodes:
        LET node == Nodes[nid]
            promote == node.limbo_promotions[origin]
        IN
        /\ PromoteIsPresent(promote)
        /\ promote.term > node.election_term

\* TODO ---------------

\* A confirmed chained PROMOTE which mattered: its CONFIRM advances the
\* applied term map in two components at once, bringing a term the node never
\* applied. A PROMOTE written over an applied limbo advances exactly one - the
\* CONFIRMs are delivered in journal order and the applied map only moves
\* forward, so all its other components are applied before it. The state
\* after the step is reachable by sequential promotions too, hence a step
\* witness.
WitnessChainConfirmed ==
    \E nid \in DataNodes:
        Cardinality({o \in NodeIDs:
            Nodes'[nid].limbo_term_map[o] > Nodes[nid].limbo_term_map[o]})
        >= 2

\* A leader confirms its own PROMOTE while a newer one is pending beside it
\* and stays live - the older-pending-confirmed-later case. Live after the
\* step means the newer one's map carries the term just applied, so its author
\* knew this PROMOTE, and could know it only as pending: applied would mean
\* this node confirmed it earlier, not in this step. The newer one is chained
\* from it and applies afterwards.
WitnessOlderPromoteConfirmedLater ==
    \E nid \in DataNodes:
        LET node == Nodes'[nid]
        IN
        /\ node.limbo_owner = nid
        /\ Nodes[nid].limbo_term < node.limbo_term
        /\ \E o \in NodeIDs \ {nid}:
            /\ PromoteIsPresent(node.limbo_promotions[o])
            /\ node.limbo_promotions[o].term > node.limbo_term
            /\ ~NodeSeesPromotePoisoned(node, node.limbo_promotions[o])

\* A limbo leader with a newer PROMOTE pending beside it. The pending demotes
\* nothing - the leader keeps writing until it observes the election term or
\* applies the CONFIRM, as the code derives the limbo state. The newer PROMOTE
\* reached the leader ahead of its term.
WitnessLeaderWithNewerPending ==
    \E nid \in DataNodes, origin \in NodeIDs:
        LET node == Nodes[nid]
            promote == node.limbo_promotions[origin]
        IN
        /\ nid # origin
        /\ node.limbo_state = LimboStateLeader
        /\ PromoteIsPresent(promote)
        /\ promote.term > node.limbo_term

\* A CONFIRM written on a delayed ack: among the counted acks is one from a
\* node which has moved to a higher term since it appended the row, and
\* without such acks the quorum is not there. The ack carried the term of the
\* append, so the origin counts it. Needs a quorum above one - the own ack
\* is never stale - so this one runs on a majority config.
WitnessConfirmOnDelayedAck ==
    \E nid \in DataNodes:
        LET node == Nodes[nid]
            FreshAcksBelowQuorum(entry) ==
                Cardinality({o \in NodeIDs:
                    /\ HasEntry(Nodes[o], entry)
                    /\ Nodes[o].election_term <= node.election_term}) < Quorum
        IN
        \/ /\ NodeConfirmPromote(nid)
           /\ FreshAcksBelowQuorum(node.limbo_promotions[nid])
        \/ /\ NodeConfirmTransaction(nid)
           /\ FreshAcksBelowQuorum(node.limbo)

\* Nothing is left to deliver over the open links.
Quiescent ==
    \A src \in DataNodes, dst \in DataNodes:
        src # dst => ~LinkIsOpenFromTo(src, dst) \/ IsCaughtUp(src, dst)

\* The replication is finished with the cluster split in two groups: every
\* pair across the cut refused each other both ways, every pair inside a
\* group still exchanges rows both ways - and is consistent, by
\* JournalDeterminesStateInvariant. A one-way refusal is not a split: the
\* refused side still follows the other.
WitnessQuiescentSplit ==
    /\ Quiescent
    /\ \E G \in SUBSET DataNodes:
        /\ G # {} /\ G # DataNodes
        /\ \A a \in G, b \in DataNodes \ G:
            a \notin Nodes[b].appliers /\ b \notin Nodes[a].appliers
        /\ \A a \in DataNodes, b \in DataNodes:
            a # b /\ (a \in G <=> b \in G) =>
                a \in Nodes[b].appliers /\ b \in Nodes[a].appliers

\* The three-promotion patterns, what a fourth term is for.

\* A chain through a chain, confirmed: the CONFIRM advances the applied term
\* map in three components at once - the last link brought two terms the node
\* never applied, on top of its own.
WitnessChainOfThree ==
    \E nid \in DataNodes:
        Cardinality({o \in NodeIDs:
            Nodes'[nid].limbo_term_map[o] > Nodes[nid].limbo_term_map[o]})
        >= 3

\* A chain revisits its origin: a leader chains its new PROMOTE over another
\* node's pending one, which is itself above the leader's own, still live,
\* pending from an earlier term - the own slot is squashed to the new one.
WitnessChainSquashesOwnPending ==
    \E nid \in DataNodes:
        LET node == Nodes[nid]
            own == node.limbo_promotions[nid]
        IN
        /\ NodeStartPromote(nid)
        /\ PromoteIsPresent(own)
        /\ ~NodeSeesPromotePoisoned(node, own)
        /\ \E other \in NodeIDs:
            LET pending == node.limbo_promotions[other]
            IN
            /\ other # nid
            /\ PromoteIsPresent(pending)
            /\ ~NodeSeesPromotePoisoned(node, pending)
            /\ pending.term > own.term

\* A poisoned pending PROMOTE replaced in its slot by its origin's later one:
\* the origin lost the term, saw the newer leadership, and promoted again.
WitnessPoisonedReplacedByOrigin ==
    \E nid \in DataNodes, origin \in NodeIDs:
        LET old == Nodes[nid].limbo_promotions[origin]
            new == Nodes'[nid].limbo_promotions[origin]
        IN
        /\ PromoteIsPresent(old)
        /\ NodeSeesPromotePoisoned(Nodes[nid], old)
        /\ PromoteIsPresent(new)
        /\ new.term > old.term

\* Three fork sides: every data node owns the limbo on itself, each having
\* confirmed its own PROMOTE alone and applied no one else's.
WitnessThreeOwners ==
    \A nid \in DataNodes:
        Nodes[nid].limbo_owner = nid /\ Nodes[nid].limbo_term > 1

================================================================================
