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

EXTENDS Integers, Sequences, FiniteSets, utils

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
    limbo, \* Implemented in the current module.
    box,   \* For access of the error.
    relay  \* For access of the lastAck.

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

\* Txn substitution.
CONSTANTS MaxClientRequests
VARIABLES txn, wal, raft
LOCAL INSTANCE txn

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

LimboInit ==
    limbo = [i \in Servers |-> [
        \* Sequence of not yet confirmed entries.
        txns |-> << >>,
        \* How owner LSN is visible on other nodes.
        vclock |-> [j \in Servers |-> 0],
        \* Owner of the limbo as seen by server.
        owner |-> Nil,
        \* The biggest promote term seen.
        promoteGreatestTerm |-> 0,
        \* Latest terms received with PROMOTE entries.
        promoteTermMap |-> [j \in Servers |-> 0],
        \* Maximal quorum lsn that has been persisted.
        confirmedLsn |-> 0,
        \* Not yet persisted confirmedLsn.
        volatileConfirmedLsn |-> 0,
        \* Biggest known confirmed lsn for each owner.
        confimedVclock |-> [j \in Servers |-> 0],
        \* Number of ACKs for the first txn in limbo.
        ackCount |-> 0,
        \* Synchro request to write.
        synchroMsg |-> EmptyGeneralMsg,
        \* Lsn of the promote currently been written.
        promoteQsyncLsn |-> 0,
        \* Term of the promote currently been written.
        promoteQsyncTerm |-> 0,
        \* Order access to the promote data. For appliers.
        promoteLatch |-> FALSE,
        \* Guard against new transactions during PROMOTE write.
        isInRollback |-> FALSE
    ]]

LOCAL LimboState(i) == [
    txns |-> limbo[i].txns, \* Also passed to txn.
    limboVclock |-> limbo[i].vclock,
    owner |-> limbo[i].owner,
    promoteGreatestTerm |-> limbo[i].promoteGreatestTerm,
    promoteTermMap |-> limbo[i].promoteTermMap,
    confirmedLsn |-> limbo[i].confirmedLsn,
    volatileConfirmedLsn |-> limbo[i].volatileConfirmedLsn,
    confimedVclock |-> limbo[i].confirmedVclock,
    ackCount |-> limbo[i].ackCount,
    synchroMsg |-> limbo[i].synchroMsg,
    promoteQsyncLsn |-> limbo[i].promoteQsyncLsn,
    promoteQsyncTerm |-> limbo[i].promoteQsyncTerm,
    promoteLatch |-> limbo[i].promoteLatch,
    isInRollback |-> limbo[i].isInRollback,
    \* Txn state.
    tId |-> txn[i].tId,
    walQueue |-> wal[i].queue,
    \* Box state,
    error |-> box[i].error,
    \* RO variables.
    i |-> i  \* Should not be used to access global variables.
]

LOCAL LimboStateApply(i, state) ==
    /\ limbo' = VarSet(i, "txns", state.txns,
                VarSet(i, "vclock", state.limboVclock,
                VarSet(i, "owner", state.owner,
                VarSet(i, "promoteGreatestTerm", state.promoteGreatestTerm,
                VarSet(i, "promoteTermMap", state.promoteTermMap,
                VarSet(i, "confirmedLsn", state.confirmedLsn,
                VarSet(i, "volatileConfirmedLsn", state.volatileConfirmedLsn,
                VarSet(i, "confirmedVclock", state.confirmedVclock,
                VarSet(i, "ackCount", state.ackCount,
                VarSet(i, "synchroMsg", state.synchroMsg,
                VarSet(i, "promoteQsyncLsn", state.promoteQsyncLsn,
                VarSet(i, "promoteQsyncTerm", state.promoteQsyncTerm,
                VarSet(i, "promoteLatch", state.promoteLatch, limbo
                ))))))))))))) \* LOL
    /\ txn

LOCAL LimboConfirm(state) ==
    IF ~LimboIsInRollback(state.synchroMsg, state.promoteLatch)
    THEN LET k == Cardinality(Servers) - ElectionQuorum
             confirmLsn == BagKthOrderStatistic(state.limboVclock, k)
             idx == SetMax({x \in 1..Len(state.txns):
                            /\ LastLsn(state.txns[x].stmts) # -1
                            /\ LastLsn(state.txns[x].stmts) <= confirmLsn})
             maxAssignedLsn == LastLsn(state.txns[idx].stmts)
             newAckCount == IF idx + 1 > Len(state.txns) THEN 0
                            ELSE IF state.txns[idx + 1].stmts[1].lsn = -1 THEN 0
                                 ELSE BagCountGreaterOrEqual(state.limboVclock,
                                        state.txns[idx + 1].stmts[1].lsn)
         IN [state EXCEPT
                !.volatileConfirmedLsn = maxAssignedLsn,
                !.ackCount = newAckCount]
    ELSE state

\* txn_limbo_ack.
LimboAck(state, source, lsn) ==
    IF /\ state.owner = state.i
       /\ Len(state.txns) > 0
       /\ lsn > state.limboVclock[source]
    THEN LET newVclock == BagSet(state.limboVclock, source, lsn)
             vclockState == [state EXCEPT !.limboVclock = newVclock]
             row == vclockState.txns[1].stmts[1]
         IN IF /\ row.lsn # -1
               /\ row.lsn <= lsn
            THEN LET countState == [vclockState EXCEPT
                         !.ackCount = vclockState.ackCount + 1]
                 IN IF countState.ackCount >= ElectionQuorum
                    THEN LimboConfirm(countState)
                    ELSE countState
            ELSE vclockState
    ELSE state

LOCAL LimboBegin(state) ==
    [state EXCEPT !.promoteLatch = TRUE]

LOCAL LimboCommit(state) ==
    [state EXCEPT !.promoteLatch = FALSE]

LOCAL LimboReqPrepare(state, entry) ==
    IF entry.type = PromoteType
    THEN [state EXCEPT !.isInRollback = TRUE]
    ELSE state

\* Part of txn_limbo_req_commit, doesn't include reading written request.
LOCAL LimboReqCommit(state, entry) ==
    LET t == entry.body.term
        newMap == IF t > state.promoteTermMap[entry.body.origin_id] THEN
                  [state.promoteTermMap EXCEPT ![entry.body.origin_id] = t]
                  ELSE state.promoteTermMap
        newGreatestTerm == IF t > state.promoteGreatestTerm
                           THEN t ELSE state.promoteGreatestTerm
    IN [state EXCEPT
        !.isInRollback = FALSE,
        !.promoteTermMap = newMap,
        !.promoteGreatestTerm = newGreatestTerm
    ]

LOCAL LimboIsSplitBrain(state, entry) ==
    /\ SplitBrainCheck = TRUE
    /\ entry.replica_id # state.owner

LOCAL LimboRaiseSplitBrainIfNeeded(state, entry) ==
    IF LimboIsSplitBrain(state, entry)
    THEN [state EXCEPT !.error = SplitBrainError]
    ELSE state

\* Part of txn_limbo_req_prepare. Checks whether synchro request can
\* be applied without yields.
LOCAL LimboReqPrepareCheck(state, entry) ==
    /\ state.promoteLatch[i] = FALSE
    /\ ~LimboIsSplitBrain(state, entry)
    /\ \/ Len(state.txns) = 0
       \/ state.txns[Len(state.txns)].stmts[1].lsn # -1

LOCAL LimboWriteStart(state, entry) ==
    /\ TxnDo(LimboReqPrepare(LimboBegin(state), entry), entry)

LimboWriteEnd(state, entry, Read(_, _)) ==
    /\ LimboReqCommit(state, entry)
    /\ Read(i, entry)
    /\ LimboCommit(i)

\* Part of apply_synchro_req.
LimboScheduleWrite(state, entry) ==
    LET newState == LimboRaiseSplitBrainIfNeeded(state, entry)
    IN IF newState.error = Nil
       THEN IF LimboReqPrepareCheck(newState, entry)
            THEN LimboWriteStart(newState, entry)
            ELSE [newState EXCEPT !.synchroMsg = GeneralMsg(entry)]
       ELSE newState

LOCAL LimboWritePromote(state, lsn, term) ==
    LET entry == XrowEntry(PromoteType, state.i, DefaultGroup,
                           DefaultFlags, [
            replica_id |-> state.owner,
            origin_id |-> state.i,
            lsn |-> lsn,
            term |-> term
        ])
    IN LimboWriteStart(state, entry)

\* txn_limbo_write_confirm.
LimboWriteConfirm(state, lsn) ==
    LET entry == XrowEntry(ConfirmType, state.i, DefaultGroup, ForceAsyncFlags,
        [
            replica_id |-> state.owner,
            origin_id |-> state.i,
            lsn |-> lsn,
            term |-> 0
        ])
    IN LimboWriteStart(state, entry)

\* Imolementation of the txn_limbo_read_confirm.
LOCAL LimboReadConfirm(state, entry) ==
    LET lsn == entry.body.lsn
        startIdx == FirstEntryWithGreaterLsnIdx(state.txns, lsn,
                                    LAMBDA tx: tx.stmts[Len(tx.stmts)].lsn)
        newLimbo == SubSeq(state.txns, startIdx, Len(state.txns))
        newVclock == BagSet(state.confirmedVclock, state.owner, lsn)
    IN [state EXCEPT
            !.confirmedLsn = lsn,
            !.confirmedVclock = newVclock,
            !.txns = newLimbo
       ]

LimboReadPromote(state, entry) ==
    LET newState == LimboReadConfirm(state, entry)
    IN [newState EXCEPT
            !.txns = << >>,
            !.owner = entry.body.origin_id,
            !.volatileConfirmedLsn = state.confirmedLsn
       ]

LOCAL LimboLastLsn(state) ==
    IF Len(state.txns) = 0
    THEN state.confirmedLsn
    ELSE LET stmts == state.txns[Len(state.txns)].stmts
         IN stmts[Len(stmts)].lsn

LOCAL LimboPromoteQsync(state, lsn, term) ==
    IF lsn # -1 \* wal_sync()
    THEN IF \A j \in Servers: \* box_wait_limbo_acked()
             \/ j = state.i
             \/ relay[state.i].lastAck[j].body.vclock[state.i] >= lsn
         THEN [LimboWritePromote(state, lsn, term)
                   EXCEPT !.promoteQsyncLsn = 0, !.promoteQsyncTerm = 0]
         ELSE [state EXCEPT !.promoteQsyncLsn = lsn,
                    !.promoteQsyncTerm = term]
    ELSE state

LimboPromoteQsyncTry(state) ==
    LimboPromoteQsync(state, state.promoteQsyncLsn, state.promoteQsyncTerm)

\* TODO: it's actually difficult to write promote, since it
\* requires written entry in wal, so in real Tarantool there will be more
\* frequent promotes. We should properly block raft worker
\* until the promote is written. Requires fiber yields.
LimboPromoteQsyncRaft(state, term) ==
    IF state.promoteQsyncLsn = 0
    THEN LimboPromoteQsync(state, LimboLastLsn(state), term)
    ELSE state

LimboProcess(state) ==
    \/ /\ state.synchroMsg.is_ready
       /\ LimboReqPrepareCheck(state, state.synchroMsg.body)
       /\ [LimboWriteStart(state, state.synchroMsg.body)
           EXCEPT !.synchroMsg = EmptyGeneralMsg]
    \/ \* limbo_bump_confirmed_lsn
       /\ state.owner = state.i
       /\ ~LimboIsInRollback(state.synchroMsg, state.promoteLatch)
       /\ state.volatileConfirmedLsn # state.confirmedLsn
       /\ LimboWriteConfirm(state, state.volatileConfirmedLsn)
    \/ /\ state.promoteQsyncLsn # 0
       /\ LimboPromoteQsyncTry(state)

LOCAL FindTxnInLimbo(newLimbo, tx) ==
    CHOOSE i \in 1..Len(newLimbo) : newLimbo[i].id = tx.id

\* Implementation of the txn_on_journal_write.
TxnOnJournalWrite(state, txnWritten) ==
    \* Implementation of the txn_on_journal_write. Assign LSNs to limbo entries.
    IF txnWritten.stmts[1].flags.wait_sync = TRUE
    THEN LET idx == FindTxnInLimbo(state.txns, txnWritten)
             newAckCount == BagCountGreaterOrEqual(
                  state.limboVclock, state.txns[idx].stmts[1].lsn)
             newState == [state EXCEPT
                  !.txns = [state.txns EXCEPT ![idx] = txnWritten],
                  !.ackCount = newAckCount
             ]
             row == txnWritten.stmts[Len(txnWritten.stmts)]
         IN IF row.flags.wait_ack = TRUE
            THEN LimboAck(newState, row.replica_id, row.lsn)
            ELSE newState
    ELSE state

LimboNext(servers) == \E i \in servers:
    LimboStateApply(i, LimboProcess(LimboState(i)))

================================================================================
