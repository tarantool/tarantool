test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

--
-- Check a constraint name for duplicate within a single
-- <CREATE TABLE> statement.
--
box.execute('CREATE TABLE t1 (i INT PRIMARY KEY);')
test_run:cmd("setopt delimiter ';'");
box.execute([[CREATE TABLE t2 (i INT, CONSTRAINT c CHECK (i > 0),
                               CONSTRAINT c PRIMARY KEY (i));]]);
box.execute([[CREATE TABLE t2 (i INT,
                               CONSTRAINT c FOREIGN KEY(i) REFERENCES t1(i),
                               CONSTRAINT c PRIMARY KEY (i));]]);
box.execute([[CREATE TABLE t2 (i INT PRIMARY KEY,
                               CONSTRAINT c CHECK (i > 0),
                               CONSTRAINT c UNIQUE (i));]]);
box.execute([[CREATE TABLE t2 (i INT PRIMARY KEY,
                               CONSTRAINT c FOREIGN KEY(i) REFERENCES t1(i),
                               CONSTRAINT c UNIQUE (i));]]);
box.execute([[CREATE TABLE t2 (i INT PRIMARY KEY,
                               CONSTRAINT c CHECK (i > 0),
                               CONSTRAINT c CHECK (i < 0));]]);
box.execute([[CREATE TABLE t2 (i INT PRIMARY KEY,
                               CONSTRAINT c FOREIGN KEY(i) REFERENCES t1(i),
                               CONSTRAINT c CHECK (i > 0));]]);
box.execute([[CREATE TABLE t2 (i INT PRIMARY KEY CONSTRAINT c REFERENCES t1(i),
                               CONSTRAINT c FOREIGN KEY(i) REFERENCES t1(i))]]);
test_run:cmd("setopt delimiter ''");

--
-- Check a constraint name for duplicate using <ALTER TABLE>
-- statement.
--
box.execute('CREATE TABLE t2 (i INT CONSTRAINT c PRIMARY KEY);')
box.execute('ALTER TABLE t2 ADD CONSTRAINT c CHECK(i > 0);')
box.execute('ALTER TABLE t2 ADD CONSTRAINT c UNIQUE(i);')
box.execute('ALTER TABLE t2 ADD CONSTRAINT c FOREIGN KEY(i) REFERENCES t1(i);')

--
-- Make sure that a constraint's name isn't kept after the
-- constraint drop.
--
box.execute('CREATE UNIQUE INDEX d ON t2(i);')
box.execute('DROP INDEX d ON t2;')
box.execute('ALTER TABLE t2 ADD CONSTRAINT d CHECK(i > 0);')
box.space.T2.ck_constraint.D:drop()
box.execute('ALTER TABLE t2 ADD CONSTRAINT d FOREIGN KEY(i) REFERENCES t1(i);')
box.execute('ALTER TABLE t2 DROP CONSTRAINT d;')

--
-- The same inside a transaction.
--
box.begin()                                                                     \
box.execute('CREATE UNIQUE INDEX d ON t2(i);')                                  \
box.execute('DROP INDEX d ON t2;')                                              \
box.execute('ALTER TABLE t2 ADD CONSTRAINT d CHECK(i > 0);')                    \
box.space.T2.ck_constraint.D:drop()                                             \
box.execute('ALTER TABLE t2 ADD CONSTRAINT d FOREIGN KEY(i) REFERENCES t1(i);') \
box.execute('ALTER TABLE t2 DROP CONSTRAINT d;')                                \
box.commit()

--
-- Circle renaming in a transaction.
--
box.execute('CREATE UNIQUE INDEX d ON t2(i);')
box.execute('ALTER TABLE t2 ADD CONSTRAINT e CHECK (i > 0);')
box.begin()                                                                     \
-- Rename CHECK E to F. Note, CHECK can't be properly renamed,                  \
-- because CHECK name is a primary key of _ck_constraint.                       \
-- It can be only dropped and created again with a new name.                    \
box.space.T2.ck_constraint.E:drop()                                             \
box.execute('ALTER TABLE t2 ADD CONSTRAINT f CHECK (i > 0);')                   \
-- Rename UNIQUE D to E.                                                        \
box.space.T2.index.D:rename('E')                                                \
-- Now E and F are occupied, and D is free. Check this.                         \
_, err1 = box.execute('ALTER TABLE t2 ADD CONSTRAINT e CHECK (i > 0);')         \
_, err2 = box.execute('CREATE UNIQUE INDEX f ON t2(i);')                        \
_, err3 = box.execute('CREATE UNIQUE INDEX d ON t2(i);')                        \
box.rollback()
-- Fail. E and F were occupied in the end of the transaction. The
-- error messages say that E was an index, not a CHECK, like it
-- was before the transaction.
err1, err2
-- Success, D was free in the end.
err3
-- After rollback ensure, that the names E and D are occupied both
-- again.
box.execute('ALTER TABLE t2 ADD CONSTRAINT d CHECK (i > 0);')
box.execute('CREATE UNIQUE INDEX e ON t2(i);')
-- F is free after rollback.
box.execute('CREATE UNIQUE INDEX f ON t2(i);')
-- Cleanup after the test case.
box.execute('DROP INDEX f ON t2;')
box.execute('DROP INDEX d ON t2;')
box.space.T2.ck_constraint.E:drop()

--
-- Make sure, that altering of an index name affects its record
-- in the space's constraint hash table.
--
box.execute('CREATE UNIQUE INDEX d ON t2(i);')
box.space.T2.index.D:rename('E')
box.execute('ALTER TABLE t2 ADD CONSTRAINT e CHECK(i > 0);')

--
-- Try rename to an already occupied name.
--
box.execute('ALTER TABLE t2 ADD CONSTRAINT f CHECK(i > 0);')
box.execute('CREATE UNIQUE INDEX g ON t2(i);')
box.space.T2.index.G:rename('F')
box.execute('DROP INDEX g ON t2;')
box.space.T2.ck_constraint.F:drop()

--
-- Make sure, that altering of an index uniqueness puts/drops
-- its name to/from the space's constraint hash table.
--
box.space.T2.index.E:alter({unique = false})
box.execute('ALTER TABLE t2 ADD CONSTRAINT e CHECK(i > 0);')
box.space.T2.index.E:alter({unique = true})
box.space.T2.ck_constraint.E:drop()
box.space.T2.index.E:alter({unique = true})
box.execute('ALTER TABLE t2 ADD CONSTRAINT e CHECK(i > 0);')

-- Alter name and uniqueness of an unique index simultaneously.
box.space.T2.index.E:alter({name = 'D', unique = false})
box.execute('CREATE UNIQUE INDEX e ON t2(i);')
-- The existing D index is not unique, but regardless of
-- uniqueness, index names should be unique in a space.
box.execute('CREATE UNIQUE INDEX d ON t2(i);')

--
-- gh-4120: Ensure that <ALTER TABLE DROP CONSTRAINT> works
-- correctly for each type of constraint.
--
-- Drop non-constraint object (non-unique index).
box.execute('CREATE INDEX non_constraint ON t2(i);')
box.execute('ALTER TABLE t2 DROP CONSTRAINT non_constraint;')
-- Drop UNIQUE constraint.
box.space.T2.index.E ~= nil
box.execute('ALTER TABLE t2 DROP CONSTRAINT e;')
box.space.T2.index.E == nil
-- Drop PRIMARY KEY constraint named "C".
box.execute('DROP INDEX non_constraint ON t2;')
box.execute('DROP INDEX d ON t2;')
box.space.T2.index.C ~= nil
box.execute('ALTER TABLE t2 DROP CONSTRAINT c;')
box.space.T2.index.C == nil
-- Drop CHECK constraint.
box.execute('ALTER TABLE t2 ADD CONSTRAINT ck_constraint CHECK(i > 0);')
box.space.T2.ck_constraint.CK_CONSTRAINT ~= nil
box.execute('ALTER TABLE t2 DROP CONSTRAINT ck_constraint;')
box.space.T2.ck_constraint.CK_CONSTRAINT == nil
-- Drop FOREIGN KEY constraint.
box.execute('ALTER TABLE t2 ADD CONSTRAINT fk FOREIGN KEY(i) REFERENCES t1(i);')
box.execute('ALTER TABLE t2 DROP CONSTRAINT fk;')
-- Drop non-existing constraint.
box.execute('ALTER TABLE t2 DROP CONSTRAINT non_existing_constraint;')

--
-- Cleanup.
--
box.execute('DROP TABLE t2;')
box.execute('DROP TABLE t1;')
