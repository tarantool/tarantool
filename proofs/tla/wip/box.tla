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

--------------------------------- MODULE box -----------------------------------

EXTENDS Integers, Sequences, FiniteSets

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS
    Servers

ASSUME Cardinality(Servers) > 0

VARIABLES
    error,          \* Critical error on the instance (only SplitBrain now).
    vclock,         \* Vclock of the current instance.
    txQueue,        \* Queue from any thread (except applier) to TX.
    txApplierQueue  \* Queue from applier thread to Tx. Applier needs separate
                    \* one, since it's crucial, that the write of synchro
                    \* request is synchronous and none of the new entries
                    \* are processed until this write is completed.

boxVars == <<error, vclock, txQueue, txApplierQueue>>

\* Applier substitution
CONSTANTS SplitBrainCheck
VARIABLES msgs, applierAckMsg, applierVclock
LOCAL applierVarsSub == <<msgs, applierAckMsg, applierVclock>>

\* Txn substitution.
CONSTANTS MaxClientRequests
VARIABLES tId, clientCtr, walQueue
LOCAL txnVarsSub == <<tId, clientCtr, walQueue>>

\* Raft substitution.
CONSTANTS ElectionQuorum, MaxRaftTerm
VARIABLES state, term, volatileTerm, vote, volatileVote, leader, votesReceived,
          isBroadcastScheduled, candidateVclock,
          relayRaftMsg
LOCAL raftVarsSub == <<state, term, volatileTerm, vote, volatileVote, leader,
                       votesReceived, isBroadcastScheduled,
                       candidateVclock, relayRaftMsg>>

\* Limbo substitution.
VARIABLES limbo, limboVclock, limboOwner, limboPromoteGreatestTerm,
          limboPromoteTermMap, limboConfirmedLsn, limboVolatileConfirmedLsn,
          limboConfirmedVclock, limboAckCount, limboSynchroMsg,
          limboPromoteLatch, relayLastAck
LOCAL limboVarsSub == <<limbo, limboVclock, limboOwner,
                        limboPromoteGreatestTerm, limboPromoteTermMap,
                        limboConfirmedLsn, limboVolatileConfirmedLsn,
                        limboConfirmedVclock, limboAckCount, limboSynchroMsg,
                        limboPromoteLatch, relayLastAck>>

LOCAL allVars == <<boxVars, applierVarsSub, txnVarsSub, raftVarsSub,
                   limboVarsSub>>

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

LOCAL INSTANCE raft
LOCAL INSTANCE limbo
LOCAL INSTANCE applier
LOCAL INSTANCE utils

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

BoxInit ==
    /\ error = [i \in Servers |-> Nil]
    /\ vclock = [i \in Servers |-> [j \in Servers |-> 0]]
    /\ txQueue = [i \in Servers |-> << >>]
    /\ txApplierQueue = [i \in Servers |-> << >>]

\* Process cbus from a thread to Tx. In TLA it's not possible to yield and wait
\* for end of writing to disk e.g, so it's processed as a separate step.

\* Implementation of the tx_status_update.
TxOnRelayUpdate(i, ack) ==
    /\ RaftProcessTerm(i, ack.term)
    /\ LimboAck(i, limbo[i], ack.replica_id, ack.vclock[i])
    \* See TxProcess for additional UNCHANGED.
    /\ UNCHANGED <<limboOwner, limboPromoteGreatestTerm,
                   limboPromoteTermMap, limboConfirmedVclock,
                   limboSynchroMsg, limboPromoteLatch>>

TxOnWrite(i, entry) ==
    LET numGlobalRows == Cardinality({j \in 1..Len(entry.rows) :
             entry.rows[j].group_id = DefaultGroup})
        numLocalRows == Cardinality({j \in 1..Len(entry.rows) :
             entry.rows[j].group_id = LocalGroup})
        \* Update vclock's 0 and i component according to number of
        \* LocalGroup and LocalGroup rows accordingly.
        newVclock == BagAdd(BagAdd(vclock[i], entry.replica_id,
                            Len(entry.rows)), 0, numLocalRows)
    IN \* Implementation of the tx_complete batch.
       /\ vclock' = [vclock EXCEPT ![i] = newVclock]
       /\ \/ /\ entry.rows[1].type = DmlType
             /\ TxnOnJournalWrite(i, entry)
             /\ ApplierSignalAckIfNeeded(i, entry, newVclock)
             /\ UNCHANGED <<raftVars>>
          \/ /\ entry.rows[1].type = ConfirmType
             /\ LimboWriteEnd(i, entry.rows[1], LimboReadConfirm)
             /\ ApplierSignalAckIfNeeded(i, entry, newVclock)
             /\ UNCHANGED <<raftVars>>
          \/ /\ entry.rows[1].type = PromoteType
             /\ LimboWriteEnd(i, entry.rows[1], LimboReadPromote)
             /\ ApplierSignalAckIfNeeded(i, entry, newVclock)
             /\ UNCHANGED <<raftVars>>
          \/ /\ entry.rows[1].type = RaftType
             /\ RaftOnJournalWrite(i, entry)
             /\ UNCHANGED <<limboVars>>

TxProcess(i) ==
    \/ IF Len(txQueue[i]) > 0
       THEN LET entry == Head(txQueue[i])
                newQueue == Tail(txQueue[i])
            IN /\ txQueue' = [txQueue EXCEPT ![i] = newQueue]
               /\ \/ /\ \/ entry.type = TxWalType
                        \/ entry.type = PromoteType
                        \/ entry.type = ConfirmType
                     /\ TxOnWrite(i, entry.body)
                  \/ /\ entry.type = TxRelayType
                     /\ TxOnRelayUpdate(i, entry.body)
       ELSE UNCHANGED <<allVars>>
    \/ IF /\ Len(txApplierQueue[i]) > 0
          /\ ~LimboIsInRollback(i, limboSynchroMsg, limboPromoteLatch)
       THEN LET entry == Head(txApplierQueue[i])
                newQueue == Tail(txApplierQueue[i])
            IN /\ txApplierQueue' = [txApplierQueue EXCEPT ![i] = newQueue]
               /\ TxOnApplierReceive(i, entry.body)
       ELSE UNCHANGED <<allVars>>

BoxNext(servers) == \E i \in servers: TxProcess(i)

================================================================================
