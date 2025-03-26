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

EXTENDS Integers, FiniteSets, Sequences

--------------------------------------------------------------------------------
\* Declaration
--------------------------------------------------------------------------------

CONSTANTS
    Servers,          \* A nonempty set of server identifiers.
    MaxClientRequests \* The max number of ClientRequests, -1 means unlimited

ASSUME Cardinality(Servers) > 0
ASSUME MaxClientRequests \in Int

VARIABLES
    \* Transaction implementation.
    tId,               \* A sequentially growing transaction id (abstraction).
    clientCtr,         \* The number of done ClientRequests
    \* WAL implementation.
    walQueue,          \* Queue from TX thread to WAL.
    \* Limbo implementation.
    limbo,             \* Sequence of not yet confirmed entries.
    limboSynchroMsg,   \* Synchro request to write.
    limboPromoteLatch, \* Order access to the promote data.
    \* Raft implementation.
    state              \* {"Follower", "Candidate", "Leader"}.

txnVars == <<tId, clientCtr>>

--------------------------------------------------------------------------------
\* Imports
--------------------------------------------------------------------------------

LOCAL INSTANCE definitions
LOCAL INSTANCE utils

--------------------------------------------------------------------------------
\* Implementation
--------------------------------------------------------------------------------

TxnInit ==
    /\ tId = [i \in Servers |-> 0]
    /\ clientCtr = 0

\* Implementation of the txn_begin.
LOCAL TxnBegin(i, stmts) ==
    \* id is fully abstractional and is not represented in real code. it's
    \* used in order to identify the transaction in the limbo. Note,
    \* that it's not tsn, which must be assigned during WalProcess, but it
    \* won't be, since it's not needed in TLA for now.
    LET id == tId[i] + 1
    IN [id |-> id, stmts |-> stmts]

\* Implementation of the txn_commit_impl for synchronous tx.
\* Adds entry to the limbo and sends it to the WAL thread for writing,
\* where it's processed by WalProcess operator.
LOCAL TxnCommit(i, txn) ==
    LET \* Set wait_sync flag if limbo is not empty.
        newStmts == IF /\ Len(limbo[i]) > 0
                       /\ txn.stmts[1].group_id # LocalGroup
                       /\ txn.stmts[1].type # NopType
                    THEN [j \in 1..Len(txn.stmts) |-> [txn.stmts[j]
                          EXCEPT !.flags = [txn.stmts[j].flags
                          EXCEPT !.wait_sync = TRUE]]]
                    ELSE txn.stmts
        newTxn == [txn EXCEPT !.stmts = newStmts]
        entry == JournalEntry(txn.stmts, newTxn)
        newWalQueue == Append(walQueue[i], entry)
        newLimbo == IF newStmts[1].flags.wait_sync = TRUE
                    THEN Append(limbo[i], newTxn)
                    ELSE limbo[i]
        doWrite == \/ newStmts[1].flags.wait_sync = FALSE
                   \/ ~LimboIsInRollback(i, limboSynchroMsg, limboPromoteLatch)
    IN IF doWrite
       THEN /\ walQueue' = [walQueue EXCEPT ![i] = newWalQueue]
            /\ limbo' = [limbo EXCEPT ![i] = newLimbo]
            \* It's impossible to update tId in TxnBegin, since it returns txn.
            /\ tId' = [tId EXCEPT ![i] = @ + 1]
       ELSE UNCHANGED <<walQueue, limbo, tId>>

TxnDo(i, entry) ==
    LET stmts == <<entry>>
    IN TxnCommit(i, TxnBegin(i, stmts))

ClientRequest(i) ==
    /\ IF /\ \/ clientCtr = -1
             \/ clientCtr < MaxClientRequests
          /\ state[i] = Leader
       THEN /\ TxnDo(i, XrowEntry(DmlType, i, DefaultGroup, SyncFlags, {}))
            /\ clientCtr' = clientCtr + 1
       ELSE UNCHANGED <<clientCtr, walQueue, limbo, tId>>
    /\ UNCHANGED <<limboSynchroMsg, limboPromoteLatch, state>>

TxnNext(servers) == \E i \in servers: ClientRequest(i)

================================================================================
