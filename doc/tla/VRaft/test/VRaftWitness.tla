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

\* A confirmed PROMOTE which was a top of a non-empty chain. Such PROMOTE on
\* confirmation will bump more the terms for more than one node.
WitnessChainConfirmed ==
    \E nid \in DataNodes:
        Cardinality({o \in NodeIDs:
            Nodes'[nid].limbo_term_map[o] > Nodes[nid].limbo_term_map[o]})
        >= 2

\* A leader confirms its own PROMOTE while a newer one is pending beside it
\* and stays live. This is a normal case. Can happen when the previous PROMOTE's
\* CONFIRM was just slow to replicate, and a newer leader had already made a
\* new PROMOTE. With a majority quorum the new PROMOTE then will always include
\* the previous one in its chain and confirming chain's tail is fine. It won't
\* contradict the head of the chain.
WitnessOlderPromoteConfirmedLater ==
    \E nid \in DataNodes:
        LET node_old == Nodes[nid]
            node_new == Nodes'[nid]
        IN
        \* This node becomes the owner on the NEXT step.
        /\ node_new.limbo_owner = nid
        /\ node_old.limbo_term < node_new.limbo_term
        \* After the NEXT step, there is still a pending valid PROMOTE of a
        \* newer term than the just confirmed one.
        /\ \E o \in NodeIDs \ {nid}:
            /\ PromoteIsPresent(node_new.limbo_promotions[o])
            /\ node_new.limbo_promotions[o].term > node_new.limbo_term
            /\ ~NodeSeesPromotePoisoned(node_new, node_new.limbo_promotions[o])
            /\ node_old.limbo_promotions[o] = node_new.limbo_promotions[o]

\* A limbo leader with a newer PROMOTE pending beside it. The pending PROMOTE
\* affects nothing. It might even never get confirmed. The current node remains
\* a writable leader.
WitnessLeaderWithNewerPending ==
    \E nid \in DataNodes, origin \in NodeIDs:
        LET node == Nodes[nid]
            promote == node.limbo_promotions[origin]
        IN
        /\ nid # origin
        /\ node.limbo_state = LimboStateLeader
        /\ PromoteIsPresent(promote)
        /\ promote.term > node.limbo_term

\* A CONFIRM written on a late but valid ack. It means that among the counted
\* acks is one from a node which has moved to a higher term since the ack was
\* 'sent'. The ack still carries its older term and must be counted by the
\* receiver.
WitnessConfirmOnDelayedAck ==
    \E nid \in DataNodes:
        LET node == Nodes[nid]
            FreshAcksBelowQuorum(entry) ==
                Cardinality({o \in NodeIDs:
                    /\ HasEntry(Nodes[o], entry)
                    /\ Nodes[o].election_term <= node.election_term}) < Quorum
        IN
        \* Quorum among latest states of the nodes is not available for the
        \* node's current term, but its pending PROMOTE gets confirmed anyway.
        \* It is only possible if one or more of the acks are delivered late.
        \/ /\ NodeConfirmPromote(nid)
           /\ FreshAcksBelowQuorum(node.limbo_promotions[nid])
        \* Same but for confirming a transaction instead of PROMOTE.
        \/ /\ NodeConfirmTransaction(nid)
           /\ FreshAcksBelowQuorum(node.limbo)

\* Nothing is left to deliver over the open links.
Quiescent ==
    \A src \in DataNodes, dst \in DataNodes:
        src # dst => ~LinkIsOpenFromTo(src, dst) \/ IsCaughtUp(src, dst)

\* An actual split-brain, literally. The replication is finished with the
\* cluster split in two groups: every pair across the cut refused each other
\* both ways, every pair inside a group still exchanges rows both ways.
WitnessQuiescentSplit ==
    /\ Quiescent
    /\ \E G \in SUBSET DataNodes:
        /\ G # {} /\ G # DataNodes
        \* No instance from one group replicates to or from another instance of
        \* the other group.
        /\ \A a \in G, b \in DataNodes \ G:
            a \notin Nodes[b].appliers /\ b \notin Nodes[a].appliers
        /\ \A a \in DataNodes, b \in DataNodes:
            \* For every 'a' and 'b' that are either both in G or in the second
            \* group, the nodes remain connected both directions.
            a # b /\ (a \in G <=> b \in G) =>
                a \in Nodes[b].appliers /\ b \in Nodes[a].appliers

\* A chain of 3 PROMOTEs confirmed at once.
WitnessChainOfThree ==
    \E nid \in DataNodes:
        LET node_new_map == Nodes'[nid].limbo_term_map
            node_old_map == Nodes[nid].limbo_term_map
        IN
            Cardinality({o \in NodeIDs: node_new_map[o] > node_old_map[o]}) >= 3

\* PROMOTE sandwich. Contains more than one PROMOTE from the same instance, with
\* one from another node in between.
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

\* Every data node is a fork side of its own: it owns the limbo on itself,
\* having confirmed its own PROMOTE alone and applied no one else's. The
\* term maps say so: no node has any term but its own.
WitnessEveryNodeOwner ==
    \A nid \in DataNodes:
        /\ Nodes[nid].limbo_owner = nid
        /\ Nodes[nid].limbo_term > 1
        /\ \A other \in DataNodes \ {nid}:
            Nodes[nid].limbo_term_map[other] = 0

================================================================================
