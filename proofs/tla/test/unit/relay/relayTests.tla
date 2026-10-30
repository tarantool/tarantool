------------------------------ MODULE relayTests -------------------------------

EXTENDS FiniteSets, TLC, utils
ASSUME LET T == INSTANCE TLC IN T!PrintT("relayTests")

CONSTANTS WalMaxRowsTest
ASSUME WalMaxRowsTest \in Nat

--------------------------------------------------------------------------------
\* Imports.
--------------------------------------------------------------------------------

CONSTANTS Servers
ASSUME Cardinality(Servers) > 1
VARIABLES relay, box, msgs, wal
allVars == <<relay, box, msgs, wal>>
INSTANCE relay

--------------------------------------------------------------------------------
\* Test RelayRead operator.
--------------------------------------------------------------------------------

LOCAL toName == CHOOSE x \in Servers: TRUE
LOCAL fromName == CHOOSE x \in Servers: x # toName

LOCAL RelayDefaultState == [
    msgsToSend |-> << >>,
    msgsToReceive |-> << >>,
    relayQueue |-> << >>,
    lastAck |-> EmptyAck(Servers),
    raftMsg |-> EmptyGeneralMsg,
    sentLsn |-> 0,
    txQueue |-> << >>,
    \* RO variables.
    toInstance |-> toName,
    walRows |-> << >>
]

\* Applier sends ack, relay saves it.
ASSUME LET ack == XrowEntry(OkType, toName, DefaultGroup, DefaultFlags, [
                vclock |-> [fromName |-> 1, toName |-> 2],
                term |-> 1
           ])
           state == [RelayDefaultState EXCEPT !.msgsToReceive = <<ack>>]
       IN RelayRead(state) = [RelayDefaultState EXCEPT !.lastAck = ack ]

--------------------------------------------------------------------------------
\* Test RelayStatusUpdate operator.
--------------------------------------------------------------------------------

\* Relay sends ack info to Tx and deletes it.
ASSUME LET ack == XrowEntry(OkType, toName, DefaultGroup, DefaultFlags, [
                vclock |-> [fromName |-> 1, toName |-> 2],
                term |-> 1
           ])
           initState == [RelayDefaultState EXCEPT
                !.msgsToReceive = <<ack>>, !.relayQueue = <<ack>>]
           ackState == RelayRead(initState)
       IN RelayStatusUpdate(ackState) = [RelayDefaultState EXCEPT
            !.txQueue = <<TxMsg(TxRelayType, ack)>>,
            !.lastAck = ack
          ]

--------------------------------------------------------------------------------
\* Test RelayProcessWalEvent operator.
--------------------------------------------------------------------------------

\* Replicate rows from the beginning of the wal.
ASSUME LET row == XrowEntry(DmlType, toName, DefaultGroup,
                            DefaultFlags, [a |-> 1])
           state == [RelayDefaultState EXCEPT !.walRows =
                <<[row EXCEPT !.lsn = 1], [row EXCEPT !.lsn = 2]>>]
       IN RelayProcessWalEvent(state) = [state EXCEPT
            !.sentLsn = 2,
            !.msgsToSend = <<[row EXCEPT !.lsn = 1], [row EXCEPT !.lsn = 2]>>
          ]

\* Replicate rows from the middle of the wal.
ASSUME LET row == XrowEntry(DmlType, toName, DefaultGroup,
                            DefaultFlags, [a |-> 1])
           state == [RelayDefaultState EXCEPT
                !.walRows = <<[row EXCEPT !.lsn = 1], [row EXCEPT !.lsn = 2]>>,
                !.sentLsn = 1
            ]
       IN RelayProcessWalEvent(state) = [state EXCEPT
            !.sentLsn = 2,
            !.msgsToSend = <<[row EXCEPT !.lsn = 2]>>
          ]

\* Local rows are replaced with NOPs.
ASSUME LET row == XrowEntry(DmlType, toName, DefaultGroup,
                            DefaultFlags, [a |-> 1])
           localRow == XrowEntry(DmlType, toName, LocalGroup,
                            DefaultFlags, [b |-> 1])
           nopRow == XrowEntry(NopType, toName, DefaultGroup, DefaultFlags, <<>>)
           state == [RelayDefaultState EXCEPT
                !.walRows = <<[row EXCEPT !.lsn = 1],
                              [localRow EXCEPT !.lsn = 2],
                              [row EXCEPT !.lsn = 3]>>,
                !.sentLsn = 1
            ]
       IN RelayProcessWalEvent(state) = [state EXCEPT
            !.sentLsn = 3,
            !.msgsToSend = <<[nopRow EXCEPT !.lsn = 2], [row EXCEPT !.lsn = 3]>>
          ]

--------------------------------------------------------------------------------
\* Test RelayRaftTrySend operator.
--------------------------------------------------------------------------------

\* Send the latest raft msg.
ASSUME LET xrow == XrowEntry(RaftType, toName, DefaultGroup, DefaultFlags, <<>>)
           state == [RelayDefaultState EXCEPT !.raftMsg = GeneralMsg(xrow)]
       IN RelayRaftTrySend(state) = [RelayDefaultState EXCEPT
            !.msgsToSend = <<xrow>>
          ]

--------------------------------------------------------------------------------
\* Specification.
--------------------------------------------------------------------------------

WalSimulate(i) ==
    /\ Len(wal[i].rows) <= WalMaxRowsTest
    /\ LET group == RandomElement({LocalGroup, DefaultGroup})
           xrow == [XrowEntry(DmlType, i, group, DefaultFlags, <<>>)
                EXCEPT !.lsn = Len(wal[i].rows)]
           newRows == Append(wal[i].rows, xrow)
       IN /\ wal' = VarSet(i, "rows", newRows, wal)
          /\ UNCHANGED <<relay, box, msgs>>

Init == /\ RelayInit
        /\ box = [i \in Servers |-> [queue |-> << >>]]
        /\ msgs = [i \in Servers |-> [j \in Servers |-> [k \in 1..2 |-> << >>]]]
        /\ wal = [i \in Servers |-> [rows |-> << >>]]

Next ==
    \/ RelayNext(Servers)
    \/ \E i \in Servers: WalSimulate(i)

Spec == Init /\ [][Next]_allVars /\ WF_allVars(Next)

--------------------------------------------------------------------------------
\* Properties.
--------------------------------------------------------------------------------

ReplNoGapsAndIncreasingLsnInv ==
    \A i, j \in Servers:
        LET rows == msgs[i][j][RelaySource]
        IN IF Len(rows) > 1 THEN
            \A k \in 2..Len(rows): rows[k].lsn = rows[k - 1].lsn + 1
           ELSE TRUE

ReplNoLocalGroupMsgs ==
    \A i, j \in Servers:
        \A k \in DOMAIN msgs[i][j][RelaySource]:
            msgs[i][j][RelaySource][k].group_id # LocalGroup

ReplSizeProp ==
    <> \A i, j \in Servers:
        \/ i = j
        \/ Len(msgs[i][j][RelaySource]) = WalMaxRowsTest

===============================================================================
