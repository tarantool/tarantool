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
        \* Order access to the promote data.
        promoteLatch |-> FALSE,
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
    promoteLatch |-> limbo[i].promoteLatch,
    limboIsOwned |-> limbo[i].owner = i,
    promoteQsyncLsn |-> limbo[i].promoteQsyncLsn,
    promoteQsyncTerm |-> limbo[i].promoteQsyncTerm,
    \* Txn state.
    tId |-> txn[i].tId,
    walQueue |-> wal[i].queue,
    \* Box state,
    error |-> Nil,
    \* RO variables.
    i |-> i, \* Should not be used to access global variables.
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
             idx == CHOOSE x \in Len(state.txns)..1 :
                  /\ x.stmts[Len(x.stmts)].lsn # -1
                  /\ x.stmts[Len(x.stmts)].lsn <= confirmLsn
             maxAssignedLsn == state.txns[idx]
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
    IF /\ state.limboIsOwned
       /\ Len(state.txns) > 0
       /\ lsn > state.limboVclock[source]
    THEN LET newVclock == BagSet(state.limboVclock, source, lsn)
             vclockState == [state EXCEPT !.limboVclock = newVclock]
         IN IF /\ vclockState.txns[1].lsn # -1
               /\ vclockState.txns[1].lsn <= lsn
            THEN LET countState == [vclockState EXCEPT
                         !.ackCount = vclockState.ackCount + 1]
                 IN IF countState.ackCount > ElectionQuorum
                    THEN LimboConfirm(countState)
                    ELSE countState
            ELSE vclockState
    ELSE state

LOCAL LimboBegin(state) ==
    [state EXCEPT !.promoteLatch = TRUE]

LOCAL LimboCommit(state) ==
    [state EXCEPT !.promoteLatch = FALSE]

LOCAL LimboReqPrepare(state, entry) ==
    [state EXCEPT !.volatileConfirmedLsn = entry.lsn]

\* Part of txn_limbo_req_commit, doesn't include reading written request.
LOCAL LimboReqCommit(state, entry) ==
    LET t == entry.body.term
        newMap == IF t > state.promoteTermMap[entry.body.origin_id] THEN
                  [state.promoteTermMap EXCEPT ![entry.body.origin_id] = t]
                  ELSE state.promoteTermMap
        newGreatestTerm == IF t > state.promoteGreatestTerm
                           THEN t ELSE state.promoteGreatestTerm
    IN [state EXCEPT
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
            term |-> term,
        ])
    IN LimboScheduleWrite(state, entry)

\* txn_limbo_write_confirm.
LOCAL LimboWriteConfirm(state, lsn) ==
    LET entry == XrowEntry(ConfirmType, state.i, DefaultGroup,
                          DefaultFlags, [
            replica_id |-> state.owner,
            origin_id |-> state.i,
            lsn |-> lsn,
            term |-> 0
        ])
    IN LimboWriteStart(state, entry)

\* First part of the txn_limbo_read_confirm. It must be splitted, since
\* this part is also used in LimboReadPromote and in TLA+ it's not possible
\* to update the same variable twice in one step (in read_confirm limbo's
\* first several entries are deleted, in read_promote, the whole limbo is
\* cleaned)
LOCAL LimboReadConfirmLsn(i, lsn) ==
    LET newVclock == BagSet(limboConfirmedVclock[i], limboOwner[i], lsn)
    IN /\ limboConfirmedLsn' = [limboConfirmedLsn EXCEPT ![i] = lsn]
       /\ limboConfirmedVclock' = [limboConfirmedVclock EXCEPT ![i] = newVclock]

\* Second part of the txn_limbo_read_confirm.
LOCAL LimboReadConfirmLsnLimbo(i, lsn) ==
    LET startIdx == FirstEntryWithGreaterLsnIdx(limbo[i], lsn,
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

LOCAL LimboLastLsn(state) ==
    IF Len(state.txns) = 0
    THEN state.confirmedLsn
    ELSE LET stmts == state.txns[Len(state.txns)].stmts
         IN stmts[Len(stmts)].lsn

LOCAL LimboPromoteQsync(state, lsn, term) ==
    IF lsn # -1 \* wal_sync()
    THEN IF \A j \in Servers: \* box_wait_limbo_acked()
             \/ j = state.i
             \/ relay[state.i].lastAck[j].body.vclock[state.i] >= lastLsn
         THEN [LimboWritePromote(state, lastLsn, term)
                   EXCEPT !.promoteQsyncLsn = 0, !.promoteQsyncTerm = term]
         ELSE [state EXCEPT !.promoteQsyncLsn = lastLsn,
                    !.promoteQsyncTerm = term]
    ELSE state

LimboPromoteQsyncTry(state) ==
    LimboPromoteQsync(state, state.promoteQsyncLsn, state.promoteQsyncTerm)

\* TODO: it's actually difficult to write promote, since it
\* requires written entry in wal, so in real Tarantool there will be
\* frequent promotes. We should properly block raft worker
\* until the promote is written. Requires proper yields.
LimboPromoteQsyncRaft(state, term) ==
    IF state.promoteQsyncLsn = 0
    THEN LimboPromoteQsync(state, LimboLastLsn(state), term)
    ELSE state

LimboPromoteQsync(state) ==
    LET lastLsn == IF state.promoteQsyncLsn = 0 THEN LimboLastLsn(state)
                   ELSE state.promoteQsyncLsn
    IN /\ lastLsn # -1 \* wal_sync()
       /\ IF \A j \in Servers: \* box_wait_limbo_acked()
              \/ j = state.i
              \/ relay[state.i].lastAck[j].body.vclock[state.i] >= lastLsn
          THEN [LimboWritePromote(state, lastLsn, term)
                    EXCEPT !.promoteQsyncLsn = 0]
          ELSE [state EXCEPT !.promoteQsyncLsn = lastLsn]

LimboProcess(i, state) ==
    \/ /\ state.synchroMsg.is_ready
       /\ LimboReqPrepareCheck(state, state.synchroMsg.body)
       /\ [LimboWriteStart(state, state.synchroMsg.body)
           EXCEPT !.synchroMsg = EmptyGeneralMsg]
    \/ \* limbo_bump_confirmed_lsn
       /\ state.limboIsOwned
       /\ ~LimboIsInRollback(state.synchroMsg, state.promoteLatch)
       /\ state.volatileConfirmedLsn # state.confirmedLsn[i]
       /\ LimboWriteConfirm(state, state.volatileConfirmedLsn[i])
    \/ /\ state.promoteQsyncLsn # 0
       /\ LimboPromoteQsyncTry(state)

LOCAL FindTxnInLimbo(newLimbo, txn) ==
    CHOOSE i \in 1..Len(newLimbo) : newLimbo[i].id = txn.id

\* Implementation of the txn_on_journal_write.
TxnOnJournalWrite(state, txnWritten) ==
    \* Implementation of the txn_on_journal_write. Assign LSNs to limbo entries.
    /\ txnWritten.stmts[1].flags.wait_sync = TRUE
    /\ LET idx == FindTxnInLimbo(state.txns, txnWritten)
           newState ==
                [state EXCEPT !.txns = [state.txns EXCEPT ![idx] = txnWritten]]
           row == txnWritten.stmts[Len(txnWritten.stmts)]
       IN IF row.flags.wait_ack = TRUE
          THEN LimboAck(newState, row.replica_id, row.lsn)
          ELSE newState

LimboNext(servers) == \E i \in servers:
    LimboStateApply(i, LimboProcess(LimboState(i)))

================================================================================
