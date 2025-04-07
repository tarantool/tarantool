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

---------------------------------- MODULE txn ----------------------------------

EXTENDS Integers, FiniteSets, Sequences, utils, definitions

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS
    Servers,          \* A nonempty set of server identifiers.
    MaxClientRequests \* The max number of ClientRequests, -1 means unlimited

ASSUME Cardinality(Servers) > 0
ASSUME MaxClientRequests \in Int

VARIABLES
    txn,   \* Implemented in the current module.
    wal,   \* For access of the queue.
    limbo, \* For access of the txns, synchroMsg, promoteLatch.
    raft   \* For acess of the state.

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

TxnInit ==
    txn = [i \in Servers |-> [
        tId |-> 0,      \* A sequentially growing transaction id (abstraction)
        clientCtr |-> 0 \* The number of done ClientRequests.
    ]]

LOCAL TxnState(i) == [
    tId |-> txn[i].tId,
    clientCtr |-> txn[i].clientCtr,
    walQueue |-> wal[i].queue,
    txns |-> limbo[i].txns,
    \* RO variables.
    raftState |-> raft[i].state,
    promoteLatch |-> limbo[i].promoteLatch,
    synchroMsg |-> limbo[i].synchroMsg
]

LOCAL TxnStateApply(i, state) ==
    /\ txn' = VarSet(i, "tId", state.tId,
              VarSet(i, "clientCtr", state.clientCtr, txn))
    /\ wal' = VarSet(i, "queue", state.walQueue, wal)
    /\ limbo' = VarSet(i, "txns", state.txns, limbo)
    /\ UNCHANGED <<raft>>

\* Implementation of the txn_begin.
LOCAL TxnBegin(state, stmts) ==
    \* id is fully abstractional and is not represented in real code. it's
    \* used in order to identify the transaction in the limbo. Note,
    \* that it's not tsn, which must be assigned during WalProcess (but it
    \* won't be, since it's not needed in TLA for now).
    LET id == state.tId + 1
    IN [id |-> id,
        \* stmts is a sequence of XrowEntries, which are written to wal,
        \* non empty, cannot have different types.
        stmts |-> stmts]

\* Implementation of the txn_commit_impl for synchronous tx.
\* Adds entry to the limbo and sends it to the WAL thread for writing,
\* where it's processed by WalProcess operator.
LOCAL TxnCommit(state, txnToApply) ==
    LET \* Set wait_sync flag if limbo is not empty.
        newStmts == IF /\ Len(state.txns) > 0
                       /\ txnToApply.stmts[1].group_id # LocalGroup
                       /\ txnToApply.stmts[1].type # NopType
                    THEN [j \in 1..Len(txnToApply.stmts) |->
                            [txnToApply.stmts[j] EXCEPT
                                !.flags = [txnToApply.stmts[j].flags
                                    EXCEPT !.wait_sync = TRUE]
                            ]
                         ]
                    ELSE txnToApply.stmts
        newTxn == [txnToApply EXCEPT !.stmts = newStmts]
        newWalQueue == Append(state.walQueue, newTxn)
        newLimboTxns == IF newStmts[1].flags.wait_sync = TRUE
                        THEN Append(state.txns, newTxn)
                        ELSE state.txns
        doWrite == \/ newStmts[1].flags.wait_sync = FALSE
                   \/ ~LimboIsInRollback(state.synchroMsg, state.promoteLatch)
    IN IF doWrite THEN [state EXCEPT
            !.tId = state.tId + 1,
            !.walQueue = newWalQueue,
            !.txns = newLimboTxns
       ] ELSE state

TxnDo(state, entry) ==
    LET stmts == <<entry>>
    IN TxnCommit(state, TxnBegin(state, stmts))

LOCAL ClientRequest(i, state) ==
    IF /\ \/ state.clientCtr = -1
          \/ state.clientCtr < MaxClientRequests
       /\ state.raftState = Leader
    THEN LET entry == XrowEntry(DmlType, i, DefaultGroup, SyncFlags, <<>>)
             stateTxn == TxnDo(state, entry)
         IN [stateTxn EXCEPT !.clientCtr = @ + 1]
    ELSE state

TxnNext(servers) == \E i \in servers:
    TxnStateApply(i, ClientRequest(i, TxnState(i)))

================================================================================
