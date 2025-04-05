------------------------------ MODULE utilsTests -------------------------------

EXTENDS Bags, utils
ASSUME LET T == INSTANCE TLC IN T!PrintT("utilsTests")

--------------------------------------------------------------------------------
\* Unit tests
--------------------------------------------------------------------------------
-------------------------
\* Test VarSet operator |
-------------------------
ASSUME LET testVar == [i \in {"s1", "s2"} |-> [a |-> "a", b |-> 1]]
       IN VarSet("s1", "a", "test", testVar) = [
            s1 |-> [a |-> "test", b |-> 1],
            s2 |-> [a |-> "a", b |-> 1]
       ]

-------------------------
\* Test SetMax operator |
-------------------------
ASSUME LET set == {2, 3, 1}
       IN SetMax(set) = 3
ASSUME LET set == {1, 1}
       IN SetMax(set) = 1
ASSUME LET set == {1, 5, 5}
       IN SetMax(set) = 5

-------------------------
\* Test BagAdd operator |
-------------------------
ASSUME BagAdd(EmptyBag, "s1", 3) = [s1 |-> 3]
ASSUME LET bag == [s1 |-> 1]
       IN BagAdd(bag, "s2", 1) = [s1 |-> 1, s2 |-> 1]
ASSUME LET bag == [s1 |-> 1]
       IN BagAdd(bag, "s1", 2) = [s1 |-> 3]

-------------------------
\* Test BagSet operator |
-------------------------
ASSUME BagSet(EmptyBag, "s1", 3) = [s1 |-> 3]
ASSUME BagSet([s1 |-> 1], "s1", 2) = [s1 |-> 2]

-----------------------------
\* Test BagCompare operator |
-----------------------------
ASSUME LET bag1 == [s1 |-> 1, s2 |-> 2]
           bag2 == [s1 |-> 1, s2 |-> 2]
       IN BagCompare(bag1, bag2) = 0
ASSUME LET bag1 == [s1 |-> 1, s2 |-> 2]
           bag2 == [s1 |-> 1, s2 |-> 1]
       IN BagCompare(bag1, bag2) = 1
ASSUME LET bag1 == [s1 |-> 1, s2 |-> 2]
           bag2 == [s1 |-> 2, s2 |-> 1]
       IN BagCompare(bag1, bag2) = -1

-------------------------------
\* Test BagCountLess operator |
-------------------------------
ASSUME LET bag == [s1 |-> 1, s2 |-> 2, s3 |-> 3]
       IN /\ BagCountLess(bag, 1) = 0
          /\ BagCountLess(bag, 2) = 1
          /\ BagCountLess(bag, 3) = 2
          /\ BagCountLess(bag, 100) = 3

--------------------------------------
\* Test BagCountLessOrEqual operator |
--------------------------------------
ASSUME LET bag == [s1 |-> 1, s2 |-> 2, s3 |-> 3]
       IN /\ BagCountLessOrEqual(bag, 0) = 0
          /\ BagCountLessOrEqual(bag, 1) = 1
          /\ BagCountLessOrEqual(bag, 2) = 2
          /\ BagCountLessOrEqual(bag, 100) = 3

---------------------------------------
\* Test BagKthOrderStatistic operator |
---------------------------------------
ASSUME LET bag == [s1 |-> 6, s2 |-> 9, s3 |-> 3, s4 |-> 7]
       IN /\ BagKthOrderStatistic(bag, 0) = 3
          /\ BagKthOrderStatistic(bag, 1) = 6
          /\ BagKthOrderStatistic(bag, 2) = 7
          /\ BagKthOrderStatistic(bag, 3) = 9
          /\ BagKthOrderStatistic(bag, 4) = -1

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
