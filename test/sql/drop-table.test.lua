test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- box.cfg()

-- create space
box.execute("CREATE TABLE zzzoobar (c1 INT, c2 INT PRIMARY KEY, c3 TEXT, c4 INT)")

box.execute("CREATE INDEX zb ON zzzoobar(c1, c3)")

-- Dummy entry
box.execute("INSERT INTO zzzoobar VALUES (111, 222, 'c3', 444)")

box.execute("DROP TABLE zzzoobar")

-- Table does not exist anymore. Should error here.
box.execute("INSERT INTO zzzoobar VALUES (111, 222, 'c3', 444)")

-- gh-3712: if space features sequence, data from _sequence_data
-- must be deleted before space is dropped.
--
box.execute("CREATE TABLE t1 (id INT PRIMARY KEY AUTOINCREMENT);")
box.execute("INSERT INTO t1 VALUES (NULL);")
box.snapshot()
test_run:cmd('restart server default')
box.execute("DROP TABLE t1;")

-- Cleanup
-- DROP TABLE should do the job

-- Debug
-- require("console").start()

--
-- gh-3592: clean-up garbage on failed CREATE TABLE statement.
--
-- Let user have enough rights to create space, but not enough to
-- create index.
--
box.schema.user.create('tmp')
box.schema.user.grant('tmp', 'create, read', 'universe')
box.schema.user.grant('tmp', 'write', 'space', '_space')
box.schema.user.grant('tmp', 'write', 'space', '_schema')

-- Number of records in _space, _index, _sequence:
space_count = #box.space._space:select()
index_count = #box.space._index:select()
sequence_count = #box.space._sequence:select()

box.session.su('tmp')
--
-- Error: user do not have rights to write in box.space._index.
-- Space that was already created should be automatically dropped.
--
box.execute('CREATE TABLE t1 (id INT PRIMARY KEY, a INT)')
-- Error: no such table.
box.execute('DROP TABLE t1')

box.session.su('admin')

--
-- Check that _space, _index and _sequence have the same number of
-- records.
--
space_count == #box.space._space:select()
index_count == #box.space._index:select()
sequence_count == #box.space._sequence:select()

--
-- Give user right to write in _index. Still have not enough
-- rights to write in _sequence.
--
box.schema.user.grant('tmp', 'write', 'space', '_index')
box.session.su('tmp')

--
-- Error: user do not have rights to write in _sequence.
--
box.execute('CREATE TABLE t2 (id INT PRIMARY KEY AUTOINCREMENT, a INT UNIQUE, b INT UNIQUE, c INT UNIQUE, d INT UNIQUE)')

box.session.su('admin')

--
-- Check that _space, _index and _sequence have the same number of
-- records.
--
space_count == #box.space._space:select()
index_count == #box.space._index:select()
sequence_count == #box.space._sequence:select()

fk_constraint_count = #box.space._fk_constraint:select()

--
-- Check that clean-up works fine after another error.
--
box.schema.user.grant('tmp', 'write', 'space')
box.session.su('tmp')

box.execute('CREATE TABLE t3(a INTEGER PRIMARY KEY);')
--
-- Error: Failed to create foreign key constraint.
--
box.execute('CREATE TABLE t4(x INTEGER PRIMARY KEY REFERENCES t3, a INT UNIQUE, c TEXT REFERENCES t3);')
box.execute('DROP TABLE t3;')

--
-- Check that _space, _index and _sequence have the same number of
-- records.
--
space_count == #box.space._space:select()
index_count == #box.space._index:select()
sequence_count == #box.space._sequence:select()
fk_constraint_count == #box.space._fk_constraint:select()

box.session.su('admin')

box.schema.user.drop('tmp')
