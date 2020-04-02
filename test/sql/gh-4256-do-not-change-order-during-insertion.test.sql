--
-- Make sure that when inserting, values are inserted in the given
-- order when ephemeral space is used.
--
CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT);
--
-- In order for this INSERT to use the ephemeral space, we created
-- this trigger.
--
CREATE TRIGGER r AFTER INSERT ON t FOR EACH ROW BEGIN SELECT 1; END
INSERT INTO t VALUES (1), (NULL), (10), (NULL), (NULL), (3), (NULL);
SELECT * FROM t;
DROP TABLE t;
