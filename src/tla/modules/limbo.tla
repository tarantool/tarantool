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

--------------------------------- MODULE limbo ---------------------------------

EXTENDS Integers, Sequences, FiniteSets

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS
    Servers,        \* A nonempty set of server identifiers.
    ElectionQuorum, \* The number of votes needed to elect a leader.
    SplitBrainCheck \* Whether SplitBrain errors should be raised.

ASSUME Cardinality(Servers) > 0
ASSUME /\ ElectionQuorum \in Nat
       /\ ElectionQuorum < Cardinality(Servers)
ASSUME SplitBrainCheck \in {TRUE, FALSE}

VARIABLES
    \* Limbo implementation.
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
    limboPromoteLatch,         \* Order access to the promote data.
    \* Tx implementation (see box module).
    error,
    \* Relay implementation
    relayLastAck

limboVars == <<limbo, limboVclock, limboOwner, limboPromoteGreatestTerm,
               limboPromoteTermMap, limboConfirmedLsn, limboConfirmedVclock,
               limboVolatileConfirmedLsn, limboAckCount, limboSynchroMsg,
               limboPromoteLatch>>

\* Txn substitution.
CONSTANTS MaxClientRequests
VARIABLES tId, clientCtr, walQueue, state
LOCAL txnVarsSub == <<tId, clientCtr, walQueue, state>>

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

LOCAL INSTANCE txn
LOCAL INSTANCE utils

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

LimboInit ==
    /\ limbo = [i \in Servers |-> << >>]
    /\ limboVclock = [i \in Servers |-> [j \in Servers |-> 0]]
    /\ limboOwner = [i \in Servers |-> Nil]
    /\ limboPromoteGreatestTerm = [i \in Servers |-> 0]
    /\ limboPromoteTermMap = [i \in Servers |-> [j \in Servers |-> 0]]
    /\ limboConfirmedLsn = [i \in Servers |-> 0]
    /\ limboConfirmedVclock = [i \in Servers |-> [j \in Servers |-> 0]]
    /\ limboVolatileConfirmedLsn = [i \in Servers |-> 0]
    /\ limboAckCount = [i \in Servers |-> 0]
    /\ limboSynchroMsg = [i \in Servers |-> EmptyGeneralMsg]
    /\ limboPromoteLatch = [i \in Servers |-> FALSE]

LOCAL LimboConfirm(i, newLimbo, newVclock) ==
    IF ~LimboIsInRollback(i, limboSynchroMsg, limboPromoteLatch)
    THEN LET k == Cardinality(Servers) - ElectionQuorum
             confirmLsn == BagNthElement(newVclock, k)
             idx == CHOOSE x \in Len(newLimbo)..1 :
                  /\ x.stmts[Len(x.stmts)].lsn # -1
                  /\ x.stmts[Len(x.stmts)].lsn <= confirmLsn
             maxAssignedLsn == newLimbo[idx]
             newAckCount == IF idx + 1 > Len(newLimbo) THEN 0
                            ELSE IF newLimbo[idx + 1].stmts[1].lsn = -1 THEN 0
                                 ELSE newLimbo[idx + 1].stmts[1].lsn
         IN /\ limboVolatileConfirmedLsn' =
                  [limboVolatileConfirmedLsn EXCEPT ![i] = maxAssignedLsn]
            /\ limboAckCount' = [limboAckCount EXCEPT ![i] = newAckCount]
    ELSE UNCHANGED <<limboVolatileConfirmedLsn, limboAckCount>>

\* txn_limbo_ack.
LimboAck(i, newLimbo, source, lsn) ==
    IF /\ limboOwner[i] = i
       /\ Len(newLimbo[i]) > 0
       /\ lsn > limboVclock[i][source]
    THEN LET newVclock == BagAssign(limboVclock[i], source, lsn)
         IN /\ limboVclock' = [limboVclock EXCEPT ![i] = newVclock]
            /\ IF /\ newLimbo[i][1].lsn # -1
                  /\ newLimbo[i][1].lsn <= lsn
               THEN LET incAckCount == limboAckCount[i] + 1
                    IN IF limboAckCount[i] + 1 > ElectionQuorum
                       THEN LimboConfirm(i, newLimbo, newVclock)
                       ELSE /\ limboAckCount' =
                                   [limboAckCount EXCEPT ![i] = incAckCount]
                            /\ UNCHANGED <<limboVolatileConfirmedLsn>>
               ELSE UNCHANGED <<limboAckCount, limboVolatileConfirmedLsn>>
    ELSE UNCHANGED <<limboVclock, limboAckCount, limboVolatileConfirmedLsn>>

LOCAL LimboBegin(i) ==
    limboPromoteLatch' = [limboPromoteLatch EXCEPT ![i] = TRUE]

LOCAL LimboCommit(i) ==
    limboPromoteLatch' = [limboPromoteLatch EXCEPT ![i] = FALSE]

LOCAL LimboReqPrepare(i, entry) ==
    limboVolatileConfirmedLsn' =
        [limboVolatileConfirmedLsn EXCEPT ![i] = entry.body.lsn]

\* Part of txn_limbo_req_commit, doesn't include reading written request.
LOCAL LimboReqCommit(i, entry) ==
    LET t == entry.body.term
        newMap == IF t > limboPromoteTermMap[i] THEN
                  [limboPromoteTermMap[i] EXCEPT ![entry.body.origin_id] = t]
                  ELSE limboPromoteTermMap[i]
        newGreatestTerm == IF t > limboPromoteGreatestTerm[i]
                           THEN t ELSE limboPromoteGreatestTerm[i]
    IN /\ limboPromoteTermMap' = [limboPromoteTermMap EXCEPT ![i] = newMap]
       /\ limboPromoteGreatestTerm' =
            [limboPromoteGreatestTerm EXCEPT ![i] = newGreatestTerm]

LOCAL LimboIsSplitBrain(i, entry) ==
    /\ SplitBrainCheck = TRUE
    /\ entry.replica_id # limboOwner[i]

LOCAL LimboRaiseSplitBrainIfNeeded(i, entry) ==
    IF LimboIsSplitBrain(i, entry)
    THEN error[i] = SplitBrainError
    ELSE UNCHANGED <<error>>

\* Part of txn_limbo_req_prepare. Checks whether synchro request can
\* be applied without yields.
LOCAL LimboReqPrepareCheck(i, entry) ==
    /\ limboPromoteLatch[i] = FALSE
    /\ ~LimboIsSplitBrain(i, entry)
    /\ \/ Len(limbo[i]) = 0
       \/ limbo[i][Len(limbo[i])].stmts[1].lsn # -1

LOCAL LimboWriteStart(i, entry) ==
    /\ LimboBegin(i)
    /\ LimboReqPrepare(i, entry)
    /\ TxnDo(i, entry)

LimboWriteEnd(i, entry, Read(_, _)) ==
    /\ LimboReqCommit(i, entry)
    /\ Read(i, entry)
    /\ LimboCommit(i)

\* Part of apply_synchro_req.
LimboScheduleWrite(i, entry) ==
    /\ LimboRaiseSplitBrainIfNeeded(i, entry)
    /\ IF /\ ~LimboIsSplitBrain(i, entry)
          /\ LimboReqPrepareCheck(i, entry)
       THEN /\ limboSynchroMsg' =
                    [limboSynchroMsg EXCEPT ![i] = GeneralMsg(entry)]
            /\ UNCHANGED <<tId, walQueue, error, limbo,
                           limboVolatileConfirmedLsn, limboPromoteLatch>>
       ELSE /\ LimboWriteStart(i, entry)
            /\ UNCHANGED <<error, limboSynchroMsg>>

LOCAL LimboWritePromote(i, lsn) ==
    LET entry == XrowEntry(PromoteType, i, DefaultGroup,
                           DefaultFlags, [
            replica_id |-> limboOwner[i],
            origin_id |-> i,
            lsn |-> lsn,
            term |-> limboPromoteTermMap
        ])
    IN LimboScheduleWrite(i, entry)

\* txn_limbo_write_confirm.
LOCAL LimboWriteConfirm(i, lsn) ==
    LET entry == XrowEntry(ConfirmType, i, DefaultGroup,
                          DefaultFlags, [
            replica_id |-> limboOwner[i],
            origin_id |-> i,
            lsn |-> lsn,
            term |-> 0
        ])
    IN LimboWriteStart(i, entry)

\* First part of the txn_limbo_read_confirm. It must be splitted, since
\* this part is also used in LimboReadPromote and in TLA+ it's not possible
\* to update the same variable twice in one step (in read_confirm limbo's
\* first several entries are deleted, in read_promote, the whole limbo is
\* cleaned)
LOCAL LimboReadConfirmLsn(i, lsn) ==
    LET newVclock == BagAssign(limboConfirmedVclock[i], limboOwner[i], lsn)
    IN /\ limboConfirmedLsn' = [limboConfirmedLsn EXCEPT ![i] = lsn]
       /\ limboConfirmedVclock' = [limboConfirmedVclock EXCEPT ![i] = newVclock]

\* Second part of the txn_limbo_read_confirm.
LOCAL LimboReadConfirmLsnLimbo(i, lsn) ==
    LET startIdx == FirstEntryMoreLsnIdx(limbo[i], lsn,
                                    LAMBDA txn: txn.stmts[Len(txn.stmts)].lsn)
        newLimbo == SubSeq(limbo[i], startIdx, Len(limbo[i]))
    IN /\ Assert(startIdx > 0, "startIdx is < 0 in LimboReadConfirmLsnLimbo")
       /\ limbo' = [limbo EXCEPT ![i] = newLimbo]

LimboReadConfirm(i, entry) ==
    /\ LimboReadConfirmLsn(i, entry.body.lsn)
    /\ LimboReadConfirmLsnLimbo(i, entry.body.lsn)

LimboReadPromote(i, entry) ==
    /\ LimboReadConfirmLsn(i, entry.body.lsn)
    /\ limbo' = [limbo EXCEPT ![i] = << >>]
    /\ limboOwner' = [limboOwner EXCEPT ![i] = entry.body.origin_id]
    /\ limboVolatileConfirmedLsn =
            [limboVolatileConfirmedLsn EXCEPT ![i] = limboConfirmedLsn[i]]

LOCAL LimboLastLsn(i) ==
    IF Len(limbo[i]) = 0
    THEN limboConfirmedLsn[i]
    ELSE LET stmts == limbo[Len(limbo[i])].stmts
         IN stmts[Len(stmts)].lsn

LimboPromoteQsync(i) ==
    LET lastLsn == LimboLastLsn(i)
    IN /\ lastLsn # -1 \* wal_sync()
       /\ \A j \in Servers: \* box_wait_limbo_acked()
            /\ j # i
            /\ relayLastAck[i][j].body.vclock[i] >= lastLsn
       /\ LimboWritePromote(i, lastLsn)

LimboProcess(i) ==
    /\ \/ IF /\ limboSynchroMsg[i].is_ready
             /\ LimboReqPrepareCheck(i, limboSynchroMsg[i])
          THEN /\ LimboWriteStart(i, limboSynchroMsg[i])
               /\ limboSynchroMsg = [limboSynchroMsg EXCEPT ![i] = EmptyGeneralMsg]
          ELSE UNCHANGED <<limboVars, txnVarsSub, error, relayLastAck>>
       \/ \* limbo_bump_confirmed_lsn
          IF /\ limboOwner[i] = i
             /\ ~LimboIsInRollback(i, limboSynchroMsg, limboPromoteLatch)
             /\ limboVolatileConfirmedLsn[i] # limboConfirmedLsn[i]
          THEN /\ LimboWriteConfirm(i, limboVolatileConfirmedLsn[i])
          ELSE UNCHANGED <<limboVars, txnVarsSub, error, relayLastAck>>
    /\ UNCHANGED
            \* Without {tId, walQueue, error, limbo, limboSynchroMsg,
            \*          limboVolatileConfirmedLsn, limboPromoteLatch}
            <<error, relayLastAck, limboVclock, limboOwner,
              limboPromoteGreatestTerm, limboPromoteTermMap, limboConfirmedLsn,
              limboConfirmedVclock, limboAckCount, limboSynchroMsg>>

LOCAL FindTxnInLimbo(newLimbo, txn) ==
    CHOOSE i \in 1..Len(newLimbo) : newLimbo[i].id = txn.id

\* Implementation of the txn_on_journal_write.
TxnOnJournalWrite(i, entry) ==
    \* Implementation of the txn_on_journal_write. Assign LSNs to limbo entries.
    IF entry.rows[1].flags.wait_sync = TRUE
    THEN LET idx == FindTxnInLimbo(limbo[i], entry.complete_data)
             newLimbo == [limbo[i] EXCEPT ![idx] = entry.complete_data]
         IN /\ limbo' = [limbo EXCEPT ![i] = newLimbo]
            /\ LET row == entry.rows[Len(entry.rows)]
               IN IF row.flags.wait_ack = TRUE
                  THEN LimboAck(i, newLimbo, row.replica_id, row.lsn)
                  ELSE UNCHANGED <<limboVclock, limboVolatileConfirmedLsn,
                                   limboAckCount>>
    ELSE UNCHANGED <<limbo, limboVclock, limboVolatileConfirmedLsn,
                     limboAckCount>>

LimboNext(servers) == \E i \in servers: LimboProcess(i)

================================================================================
