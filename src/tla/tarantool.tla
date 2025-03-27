/*
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

------------------------------- MODULE tarantool -------------------------------

EXTENDS Integers, Bags, FiniteSets, Sequences, TLC

CONSTANTS
    Servers,             \* A nonempty set of server identifiers.
    ElectionQuorum,      \* The number of votes needed to elect a leader.
    MaxClientRequests,   \* The max number of ClientRequests, -1 means unlimited
    SplitBrainCheck,     \* Whether SplitBrain errors should be raised.
    MaxRaftTerm,         \* The maximum term, which can be achieved in the rs.
    MaxHeartbeatsPerTerm \* The maximum number of hearbeats per term.

ASSUME Cardinality(Servers) > 0
ASSUME /\ ElectionQuorum \in Nat
       /\ ElectionQuorum < Cardinality(Servers)
ASSUME MaxClientRequests \in Int
ASSUME SplitBrainCheck \in {TRUE, FALSE}
ASSUME MaxRaftTerm \in Int
ASSUME MaxHeartbeatsPerTerm \in Int

-------------------------------------------------------------------------------
\* Global variables
-------------------------------------------------------------------------------

\* Note, that all variables of another module must be instantiated in the
\* one, to which it's imported (see part 4.2 of Specifying Systems).

\* msgs[sender][receiver][source]. Source is needed, since every instance
\* has 2 connections: first - relay, second - applier. So source = 1 means
\* that msg was written by relay, 2 - by applier. Relay writes to 1, reads
\* from 2. Applier writes to 2, reads from 1.
VARIABLE msgs

-------------------------------------------------------------------------------
\* Per-server variables (operators with server argument)
-------------------------------------------------------------------------------

\* Box implementation.
VARIABLES
    error,          \* Critical error on the instance.
    vclock,         \* Vclock of the current instance.
    txQueue,        \* Queue from any thread (except applier) to TX.
    txApplierQueue  \* Queue from applier thread to Tx. Applier needs separate
                    \* one, since it's crucial, that the write of synchro
                    \* request is synchronous and none of the new entries
                    \* are processed until this write is completed.

\* Txn implementation.
VARIABLES
    tId,      \* A sequentially growing transaction id (abstraction).
    clientCtr \* The number of done ClientRequests

\* Wal implementation.
VARIABLES
    wal,      \* Sequence of log entries, persisted in WAL.
    walQueue  \* Queue from TX thread to WAL.

\* Limbo implementation.
VARIABLES
    limbo,                     \* Sequence of not yet confirmed entries.
    limboVclock,               \* How owner LSN is visible on other nodes.
    limboOwner,                \* Owner of the limbo as seen by server.
    limboPromoteGreatestTerm,  \* The biggest promote term seen.
    limboPromoteTermMap,       \* Latest terms received with PROMOTE entries.
    limboConfirmedLsn,         \* Maximal quorum lsn that has been persisted.
    limboVolatileConfirmedLsn, \* Not yet persisted confirmedLsn.
    limboConfirmedVclock,      \* Biggest known confirmed lsn for each owner.
    limboAckCount,             \* Number of ACKs for the first txn in limbo.
    limboSynchroMsg,           \* Synchro request to write.
    limboPromoteLatch          \* Order access to the promote data.

\* Raft implementation.
VARIABLES
    state,                \* {"Follower", "Candidate", "Leader"}.
    term,                 \* The current term number of each node.
    volatileTerm,         \* Not yet persisted term.
    vote,                 \* Node vote in its term.
    volatileVote,         \* Not yet persisted vote.
    leader,               \* The leader as known by node.
    votesReceived,        \* The set of nodes, which voted for the candidate.
    leaderWitnessMap,     \* A bitmap of sources, which see leader in this term.
    isBroadcastScheduled, \* Whether state should be broadcasted to other nodes.
    candidateVclock       \* Vclock of the candidate for which node is voting.

\* Relay implementation.
VARIABLES
    relaySentLsn,     \* Last sent LSN to the peer. See relay->r->cursor.
    relayLastAck,     \* Last received ack from replica.
    relayRaftMsg,     \* Raft message for broadcast.
    relayHeartbeatCtr \* The number of heartbeats done.

\* Applier implementation
VARIABLES
    applierAckMsg, \* Whether applier needs to send acks.
    applierVclock  \* Implementation of the replicaset.applier.vclock

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

LOCAL INSTANCE definitions
LOCAL INSTANCE raft
LOCAL INSTANCE applier
LOCAL INSTANCE box
LOCAL INSTANCE limbo
LOCAL INSTANCE relay
LOCAL INSTANCE txn
LOCAL INSTANCE wal

allVars == <<msgs, applierVars, boxVars, limboVars, raftVars, relayVars,
             txnVars, walVars>>

-------------------------------------------------------------------------------
\* Initial values for all variables
-------------------------------------------------------------------------------

Init == /\ msgs = [i \in Servers |-> [j \in Servers |-> [k \in 1..2 |-> <<>>]]]
        /\ ApplierInit
        /\ BoxInit
        /\ LimboInit
        /\ RaftInit
        /\ RelayInit
        /\ TxnInit
        /\ WalInit

-------------------------------------------------------------------------------
\* Specification
-------------------------------------------------------------------------------

AliveServers == {i \in Servers : error[i] = Nil}

RaftNextTnt ==
   /\ RaftNext(AliveServers)
   /\ UNCHANGED <<applierAckMsg, applierVclock, vclock, txQueue,
                  relayHeartbeatCtr, relayRaftMsg, relaySentLsn,
                  txApplierQueue, wal>>

BoxNextTnt ==
    /\ BoxNext(AliveServers)
    /\ UNCHANGED <<wal, relayHeartbeatCtr, relaySentLsn>>

LimboNextTnt ==
    /\ LimboNext(AliveServers)
    /\ UNCHANGED <<applierAckMsg, applierVclock, candidateVclock,
                   isBroadcastScheduled, leader, leaderWitnessMap, msgs,
                   relayHeartbeatCtr, relayRaftMsg, relaySentLsn, term,
                   txApplierQueue, txQueue, vclock, volatileTerm, volatileVote,
                   vote, votesReceived, wal>>

TxnNextTnt ==
    /\ TxnNext(AliveServers)
    /\ UNCHANGED <<applierAckMsg, applierVclock, candidateVclock, error,
                   isBroadcastScheduled, leader, leaderWitnessMap,
                   limboAckCount, limboConfirmedLsn, limboConfirmedVclock,
                   limboOwner, limboPromoteGreatestTerm, limboPromoteTermMap,
                   limboVclock, limboVolatileConfirmedLsn, msgs,
                   relayHeartbeatCtr, relayLastAck, relayRaftMsg, relaySentLsn,
                   term, txApplierQueue, txQueue, vclock, volatileTerm,
                   volatileVote, vote, votesReceived, wal>>

WalNextTnt ==
    /\ WalNext(AliveServers)
    \* doesn't add states, why
    /\ UNCHANGED <<>>

RelayNextTnt ==
    /\ RelayNext(AliveServers)
    /\ UNCHANGED <<applierAckMsg, applierVclock, candidateVclock, clientCtr,
                   error, isBroadcastScheduled, leader, leaderWitnessMap,
                   limbo, limboAckCount, limboConfirmedLsn,
                   limboConfirmedVclock, limboOwner, limboPromoteGreatestTerm,
                   limboPromoteLatch, limboPromoteTermMap, limboSynchroMsg,
                   limboVclock, limboVolatileConfirmedLsn, state, tId,
                   txApplierQueue, volatileTerm, volatileVote, vote,
                   votesReceived, walQueue>>

ApplierNextTnt ==
    /\ ApplierNext(AliveServers)
    \* doesn't add states, why
    /\ UNCHANGED <<>>

\* Defines how the variables may transition.
Next ==
    \* TX thread.
    \/ RaftNextTnt
    \/ BoxNextTnt
    \/ LimboNextTnt
    \/ TxnNextTnt
    \* WAL thread.
    \/ WalNextTnt
    \* Relay threads (from i to j)
    \/ RelayNextTnt \* (doesn't work)
    \* Applier threads (from j to i).
    \/ ApplierNextTnt

\* Start with Init and transition according to Next. By specifying WF (which
\* stands for Weak Fairness) we're including the requirement that the system
\* must eventually take a non-stuttering step whenever one is possible.
Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

================================================================================

Follow-ups:
    * Restart, requires implementation of the SUBSCRIBE, since
      cursor on relay is not up to date. Can update sentLsn explicitly!
    * Reconfiguration of a replicaset: add/remove replicas:
        - Probably requires implementation of the JOIN, SUBSCRIBE,
          snapshots, xlogs.
