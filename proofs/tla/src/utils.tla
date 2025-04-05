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

--------------------------------- MODULE utils ---------------------------------

EXTENDS Integers, Sequences, FiniteSets, TLC, definitions

--------------------------------------------------------------------------------
\* General helper operators
--------------------------------------------------------------------------------

\* Sets variable var to value for server i.
VarSet(i, var, value) ==
    (i :> value) @@ var

\* Sends the xrow message from i to j. See details about messaging system in
\* the tarantool module, alongside with the declaration of the msgs variable.
Send(messages, i, j, source, xrow) ==
    LET newMsgs == Append(messages[i][j][source], xrow)
    IN [messages EXCEPT ![i][j][source] = newMsgs]

\* Returns the maximum value in the set 'S'.
SetMax(S) ==
    CHOOSE m \in S: \A n \in S: n <= m

--------------------------------------------------------------------------------
\* Vclock helpers
--------------------------------------------------------------------------------
\* Bag module is used for vclocks (see Bag standard module, basically mutiset).

\* Given a bag and entry e, return a new bag with v more e in it.
BagAdd(bag, e, v) ==
    IF e \in DOMAIN bag
    THEN [bag EXCEPT ![e] = bag[e] + v]
    ELSE bag @@ (e :> v)

\* Assigns bag[e] = v.
BagSet(bag, e, v) == VarSet(e, bag, v)

\* Compares two bags b1 and b2. Returns:
\*   0  if for all s in Servers b1[s] = b2[s]
\*   1  if for all s in Servers b1[s] >= b2[s] and b1 differs in at least one s
\*  -1  otherwise
BagCompare(b1, b2) ==
    IF /\ \A s \in DOMAIN b1: b1[s] >= b2[s]
       /\ Assert(DOMAIN b1 = DOMAIN b2, "Domains are not equal in compare")
    THEN IF \A s \in DOMAIN b1: b1[s] = b2[s] THEN 0 ELSE 1
    ELSE -1

\* Returns the number of elements < x.
BagCountLess(b, x) ==
    Cardinality({i \in DOMAIN b: b[i] < x})

\* Returns the number of elements <= x.
BagCountLessOrEqual(b, x) ==
    Cardinality({i \in DOMAIN b: b[i] <= x})

\* Returns kth order statistic of the bag.
BagKthOrderStatistic(b, k) ==
    IF k >= Cardinality(DOMAIN b) THEN -1
    ELSE LET idx == CHOOSE x \in DOMAIN b:
                   /\ BagCountLess(b, b[x]) <= k
                   /\ BagCountLessOrEqual(b, b[x]) > k
         IN b[idx]

-------------------------------------------------------------------------------
\* Structure declarations
-------------------------------------------------------------------------------

\* Return xrow entry. It's written to WAL and replicated.
\* LSN is assigned during WalProcess.
\*  - `xrowType` is DmlXrowType/RaftXrowType/SynchroXrowType;
\*  - `replica_id` is id of the replica which made xrow.
\*  - `groupId` is DefaultGroup/LocalGroup;
\*  - `body` is any function, depends on `xrowType`.
XrowEntry(xrowType, replica_id, groupId, flags, body) == [
    type |-> xrowType,
    replica_id |-> replica_id,
    group_id |-> groupId,
    lsn |-> -1,
    flags |-> flags,
    body |-> body
]

XrowEntryIsGlobal(e) ==
    IF e.group_id = DefaultGroup THEN TRUE ELSE FALSE

\* LSN of the last entry in the log or 0 if the log is empty.
LastLsn(xlog) ==
    IF Len(xlog) = 0 THEN 0 ELSE xlog[Len(xlog)].lsn

\* Entry, which is sent to WAL thread. Entry cannot have rows of different
\* types (abstraction). Put in walQueue.
\*  - `rows` is a sequence of XrowEntries, which are written to wal, non empty;
\*  - `txn` is either created by TxnBegin or Nil.
JournalEntry(rows, txn) == [
    rows |-> rows,
    \* Txn is complete_data in struct journal_entry. Used to assign
    \* LSNs to limbo after entry is written. rows are written explicitly.
    complete_data |-> txn
]

\* Msg from any thread to Tx thread. Put in TxQueue. Abstraction to process
\* different Tx events.
TxMsg(txEntryType, body) == [
    type |-> txEntryType,
    body |-> body
]

\* Used in applierAckMsg and relayRaftMsg, limboSynchroMsg.
GeneralMsg(body) == [
    is_ready |-> TRUE,
    body |-> body
]

GreaterCmp(a, b) == a > b
EqualCmp(a, b) == a = b

FirstEntryLsnIdx(w, lsn, Op(_), Cmp(_, _)) ==
    IF \E k \in 1..Len(w) : Cmp(Op(w[k]), lsn)
    THEN CHOOSE k \in 1..Len(w) : Cmp(Op(w[k]), lsn)
    ELSE -1

FirstEntryWithGreaterLsnIdx(w, lsn, Op(_)) ==
    FirstEntryLsnIdx(w, lsn, Op, GreaterCmp)

\* TODO: I don't need it?
FirstEntryWithEqualLsnIdx(w, lsn, Op(_)) ==
    FirstEntryLsnIdx(w, lsn, Op, EqualCmp)

EmptyGeneralMsg == [is_ready |-> FALSE, body |-> <<>>]
EmptyAck(servers) == XrowEntry(OkType, Nil, DefaultGroup, DefaultFlags,
                               [vclock |-> [i \in servers |-> 0], term |-> 0])

-------------------------------------------------------------------------------
\* Solving cyclic dependencies
-------------------------------------------------------------------------------

\* Cyclic dependency, if placed in limbo.tla: txn -> limbo -> txn
LimboIsInRollback(i, synchroMsg, promoteLatch) ==
    \/ synchroMsg[i] # EmptyGeneralMsg
    \/ promoteLatch[i] = TRUE

================================================================================
