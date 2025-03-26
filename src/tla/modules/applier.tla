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

-------------------------------- MODULE applier --------------------------------

EXTENDS Integers, Sequences, FiniteSets

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS
    Servers,        \* A nonempty set of server identifiers.
    SplitBrainCheck \* Whether SplitBrain errors should be raised.

ASSUME Cardinality(Servers) > 0
ASSUME SplitBrainCheck \in {TRUE, FALSE}

VARIABLES
    msgs,
    \* Applier implementation.
    applierAckMsg, \* Whether applier needs to send acks.
    applierVclock, \* Implementation of the replicaset.applier.vclock
    \* Tx implementation (see box module).
    txApplierQueue

applierVars == <<applierAckMsg, applierVclock>>

\* Txn substitution.
CONSTANTS MaxClientRequests
VARIABLES tId, clientCtr, walQueue
LOCAL txnVarsSub == <<tId, clientCtr, walQueue>>

\* Raft substitution.
CONSTANTS ElectionQuorum, MaxRaftTerm
VARIABLES state, term, volatileTerm, vote, volatileVote, leader, votesReceived,
          leaderWitnessMap, isBroadcastScheduled, candidateVclock, vclock,
          relayRaftMsg
LOCAL raftVarsSub == <<state, term, volatileTerm, vote, volatileVote, leader,
                       votesReceived, leaderWitnessMap, isBroadcastScheduled,
                       candidateVclock, vclock, relayRaftMsg>>

\* Limbo substitution.
VARIABLES limbo, limboVclock, limboOwner, limboPromoteGreatestTerm,
          limboPromoteTermMap, limboConfirmedLsn, limboVolatileConfirmedLsn,
          limboConfirmedVclock, limboAckCount, limboSynchroMsg,
          limboPromoteLatch, error, relayLastAck
LOCAL limboVarsSub == <<limbo, limboVclock, limboOwner,
                        limboPromoteGreatestTerm, limboPromoteTermMap,
                        limboConfirmedLsn, limboVolatileConfirmedLsn,
                        limboConfirmedVclock, limboAckCount, limboSynchroMsg,
                        limboPromoteLatch, error, relayLastAck>>

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

LOCAL INSTANCE txn
LOCAL INSTANCE raft
LOCAL INSTANCE limbo
LOCAL INSTANCE utils

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

ApplierInit ==
    /\ applierAckMsg = [i \in Servers |-> [j \in Servers |-> EmptyGeneralMsg]]
    /\ applierVclock = [i \in Servers |-> [j \in Servers |-> 0]]

\* Implementation of the applier_thread_writer_f
ApplierWrite(i, j) ==
    /\ applierAckMsg[i][j].is_ready = TRUE
    /\ Send(msgs, i, j, ApplierSource, applierAckMsg[i][j].body)
    /\ LET newMsg == [applierAckMsg[i][j] EXCEPT !.is_ready = FALSE]
       IN applierAckMsg' = [applierAckMsg EXCEPT ![i][j] = newMsg]
    /\ UNCHANGED
        \* Without {msgs, applierAckMsg}
        <<applierVclock, txApplierQueue>>

ApplierRead(i, j) ==
    /\ Len(msgs[j][i][RelaySource]) > 0
    /\ LET entry == Head(msgs[j][i][RelaySource])
           newQueue == Append(txApplierQueue[i], TxMsg(TxApplierType, entry))
       IN /\ txApplierQueue' = [txApplierQueue EXCEPT ![i] = newQueue]
          /\ msgs' = [msgs EXCEPT ![j][i][RelaySource] =
                          Tail(msgs[j][i][RelaySource])]
          /\ UNCHANGED
                \* Without {msgs, txApplierQueue}
                <<applierAckMsg, applierVclock>>

ApplierProcess(i, j) ==
    /\ \/ ApplierWrite(i, j)
       \/ ApplierRead(i, j)
    /\ UNCHANGED <<msgs, txnVarsSub, raftVarsSub, limboVarsSub, txApplierQueue>>

ApplierProcessHeartbeat(i, entry) ==
    RaftProcessHeartbeat(i, entry.replica_id)

ApplierSynchroIsSplitBrain(i, entry) ==
    /\ SplitBrainCheck = TRUE
    /\ limboPromoteTermMap[entry.replica_id] # limboPromoteGreatestTerm[i]
    /\ entry.type = DmlType

\* Part of applier_synchro_filter_tx, raise Split Brain error.
ApplierSynchroRaiseSplitBrainIfNeeded(i, entry) ==
    IF ApplierSynchroIsSplitBrain(i, entry)
    THEN error' = [error EXCEPT ![i] = SplitBrainError]
    ELSE UNCHANGED <<error>>

\* Part of applier_synchro_filter_tx, NOPify entries.
ApplierSynchroNopifyTx(i, entry) ==
    LET skipNopify == /\ limboPromoteTermMap[entry.replica_id] =
                            limboPromoteGreatestTerm[i]
                      /\ \/ entry.type = PromoteType
                         \/ /\ entry.type = ConfirmType
                            /\ entry.body.lsn >
                                limboConfirmedVclock[i][entry.replica_id]
    IN IF skipNopify THEN entry
       ELSE [entry EXCEPT !.type = NopType, !.body = <<>>]

ApplierNotInSynchroWrite(i) ==
    ~LimboIsInRollback(i, limboSynchroMsg, limboPromoteLatch)

ApplierApplyTx(i, entry) ==
    /\ ApplierSynchroRaiseSplitBrainIfNeeded(i, entry)
    /\ IF /\ ~ApplierSynchroIsSplitBrain(i, entry)
          /\ entry.lsn > applierVclock[i][entry.replica_id]
       THEN LET newEntry == ApplierSynchroNopifyTx(i, entry)
            IN /\ applierVclock = [applierVclock EXCEPT
                                   ![i][entry.replica_id] = newEntry.lsn]
               /\ IF \/ newEntry.type = DmlType
                     \/ newEntry.type = NopType
                  THEN /\ TxnDo(i, newEntry)
                  ELSE /\ LimboScheduleWrite(i, newEntry)
                       /\ UNCHANGED <<>>
       ELSE UNCHANGED <<applierVclock>>
    /\ UNCHANGED
        \* Without {error, applierVclock, TxnDo: {wal, walQueue, limbo, tId},
        \*          LimboScheduleWrite: {limboSynchroMsg, {tId, walQueue, error,
        \*          limbo, limboSynchroMsg, limboVolatileConfirmedLsn,
        \*          limboPromoteLatch}}}
        <<msgs, raftVars,
          vclock, txApplierQueue, limboVclock, limboOwner,
          limboPromoteGreatestTerm, limboPromoteTermMap, limboConfirmedLsn,
          limboConfirmedVclock, limboAckCount, limboSynchroMsg>>

\* Implementation of the applier_process_batch.
TxOnApplierReceive(i, entry) ==
    \/ /\ entry.lsn # -1 \* DmlType, PromoteType, ConfirmType
       /\ ApplierApplyTx(i, entry)
    \/ /\ entry.type = OkType
       /\ ApplierProcessHeartbeat(i, entry)
    \/ /\ entry.type = RaftType
       /\ RaftProcessMsg(i, entry)

ApplierSignalAck(i, j, ackVclock) ==
    LET entry == XrowEntry(OkType, i, DefaultGroup, DefaultFlags, [
        vclock |-> ackVclock,
        term |-> term[i]
    ])
    IN applierAckMsg = [applierAckMsg EXCEPT ![i][j] = GeneralMsg(entry)]

\* Implementation of the applier_on_wal_write. Sends acks.
ApplierSignalAckIfNeeded(i, entry, ackVclock) ==
    /\ IF entry.replica_id # i
       THEN ApplierSignalAck(i, entry.replica_id, ackVclock)
       ELSE UNCHANGED <<applierAckMsg>>
    /\ UNCHANGED <<relayLastAck, relayRaftMsg, applierVclock>>

ApplierNext(servers) == \E i, j \in servers: ApplierProcess(i, j)

================================================================================
