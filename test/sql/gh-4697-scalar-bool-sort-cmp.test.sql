CREATE TABLE test (s1 INTEGER PRIMARY KEY, s2 SCALAR UNIQUE, s3 SCALAR);
INSERT INTO test VALUES (0, 1, 1);
INSERT INTO test VALUES (1, 1.1, 1.1);
INSERT INTO test VALUES (2, true, true);
INSERT INTO test VALUES (3, NULL, NULL);

--
-- gh-4679: Make sure that boolean precedes any number within
-- scalar. Result with order by indexed (using index) and
-- non-indexed (using no index) must be the same.
--
SELECT s2, TYPEOF(s2) FROM SEQSCAN test ORDER BY s2;
SELECT s3, TYPEOF(s3) FROM SEQSCAN test ORDER BY s3;
