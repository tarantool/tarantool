------------------------------ MODULE utilsTests -------------------------------

EXTENDS Bags, utils
ASSUME LET T == INSTANCE TLC IN T!PrintT("utilsTests")

--------------------------------------------------------------------------------
\* Test VarSet operator.
--------------------------------------------------------------------------------
ASSUME LET testVar == [i \in {"s1", "s2"} |-> [a |-> "a", b |-> 1]]
       IN VarSet("s1", "a", "test", testVar) = [
            s1 |-> [a |-> "test", b |-> 1],
            s2 |-> [a |-> "a", b |-> 1]
       ]

----------------------------------------------
\* Test FirstEntryWithGreaterLsnIdx operator |
----------------------------------------------
ASSUME LET entry == XrowEntry(DmlType, "s1", DefaultGroup, SyncFlags, {})
           e1 == [entry EXCEPT !.lsn = 1]
           e2 == [entry EXCEPT !.lsn = 2]
           e3 == [entry EXCEPT !.lsn = 3]
           wal == <<e1, e2, e3>>
       IN /\ FirstEntryWithGreaterLsnIdx(wal, 0, LAMBDA x: x.lsn) = 1
          /\ FirstEntryWithGreaterLsnIdx(wal, 1, LAMBDA x: x.lsn) = 2
          /\ FirstEntryWithGreaterLsnIdx(wal, 3, LAMBDA x: x.lsn) = -1

================================================================================
