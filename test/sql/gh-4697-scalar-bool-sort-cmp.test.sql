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
SELECT s2, typeof(s2) FROM test ORDER BY s2;
SELECT s3, typeof(s3) FROM test ORDER BY s3;
