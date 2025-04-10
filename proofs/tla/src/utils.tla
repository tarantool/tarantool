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

EXTENDS Integers, Sequences, definitions

--------------------------------------------------------------------------------
\* General helper operators.
--------------------------------------------------------------------------------

\* Sets root.variable = value for server i.
VarSet(i, variable, value, root) ==
    [root EXCEPT ![i] = [root[i] EXCEPT ![variable] = value]]

-------------------------------------------------------------------------------
\* Structure declarations.
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

\* LSN of the last entry in the log or 0 if the log is empty.
LastLsn(xlog) ==
    IF Len(xlog) = 0 THEN 0 ELSE xlog[Len(xlog)].lsn

\* Msg from any thread to Tx thread. Put in TxQueue. Abstraction to process
\* different Tx events.
TxMsg(txEntryType, body) == [
    type |-> txEntryType,
    body |-> body
]

EmptyGeneralMsg == [is_ready |-> FALSE, body |-> <<>>]

-------------------------------------------------------------------------------
\* Solving cyclic dependencies.
-------------------------------------------------------------------------------

\* Cyclic dependency, if placed in limbo.tla: txn -> limbo -> txn.
LimboIsInRollback(synchroMsg, promoteLatch) ==
    \/ synchroMsg # EmptyGeneralMsg
    \/ promoteLatch = TRUE

================================================================================
