env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default with cleanup=1')


-- Check that tuple inserted into _fk_constraint is FK constrains
-- valid data.
--
box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a INT, b INT);")
box.execute("CREATE UNIQUE INDEX i1 ON t1(a);")
box.execute("CREATE TABLE t2 (a INT, b INT, id INT PRIMARY KEY);")
box.execute("CREATE VIEW v1 AS SELECT * FROM t1;")

-- Parent and child spaces must exist.
--
t = {'fk_1', 666, 777, false, 'simple', 'restrict', 'restrict', {0}, {1}}
box.space._fk_constraint:insert(t)

parent_id = box.space._space.index.name:select('T1')[1]['id']
child_id = box.space._space.index.name:select('T2')[1]['id']
view_id = box.space._space.index.name:select('V1')[1]['id']

-- View can't reference another space or be referenced by another space.
--
t = {'fk_1', child_id, view_id, false, 'simple', 'restrict', 'restrict', {0}, {1}}
box.space._fk_constraint:insert(t)
t = {'fk_1', view_id, parent_id, false, 'simple', 'restrict', 'restrict', {0}, {1}}
box.space._fk_constraint:insert(t)
box.execute("DROP VIEW v1;")

-- Match clause can be only one of: simple, partial, full.
--
t = {'fk_1', child_id, parent_id, false, 'wrong_match', 'restrict', 'restrict', {0}, {1}}
box.space._fk_constraint:insert(t)

-- On conflict actions can be only one of: set_null, set_default,
-- restrict, cascade, no_action.
t = {'fk_1', child_id, parent_id, false, 'simple', 'wrong_action', 'restrict', {0}, {1}}
box.space._fk_constraint:insert(t)
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'wrong_action', {0}, {1}}
box.space._fk_constraint:insert(t)

-- Temporary restriction (until SQL triggers work from Lua):
-- referencing space must be empty.
--
box.execute("INSERT INTO t2 VALUES (1, 2, 3);")
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {2}, {1}}
box.space._fk_constraint:insert(t)
box.execute("DELETE FROM t2;")

-- Links must be specififed correctly.
--
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {}, {}}
box.space._fk_constraint:insert(t)
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {2}, {1,2}}
box.space._fk_constraint:insert(t)
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {13}, {1}}
box.space._fk_constraint:insert(t)
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {'crash'}, {'crash'}}
box.space._fk_constraint:insert(t)

-- Referenced fields must compose unique index.
--
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {0, 1}, {1, 2}}
box.space._fk_constraint:insert(t)

-- Referencing and referenced fields must feature compatible types.
-- Temporary, in SQL all fields except for INTEGER PRIMARY KEY
-- are scalar.
--
--t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {1}, {0}}
--box.space._fk_constraint:insert(t)

-- Each referenced column must appear once.
--
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {0, 1}, {1, 1}}
box.space._fk_constraint:insert(t)

-- Successful creation.
--
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {0}, {1}}
t = box.space._fk_constraint:insert(t)

-- Implicitly referenced index can't be dropped,
-- ergo - space can't be dropped until it is referenced.
--
box.execute("DROP INDEX i1 on t1;")

-- Referenced index can't be altered as well, if alter leads to
-- rebuild of index (e.g. index still can be renamed).
box.space._index:replace{512, 1, 'I1', 'tree', {unique = true}, {{field = 0, type = 'unsigned', is_nullable = true}}}
box.space._index:replace{512, 1, 'I2', 'tree', {unique = true}, {{field = 1, type = 'unsigned', is_nullable = true}}}

-- Finally, can't drop space until it has FK constraints,
-- i.e. by manual removing tuple from _space.
-- But drop() will delete constraints.
--
box.space.T2.index[0]:drop()
box.space._space:delete(child_id)
box.space.T2:drop()

-- Make sure that constraint has been successfully dropped,
-- so we can drop now and parent space.
--
box.space._fk_constraint:select()
box.space.T1:drop()

-- Create several constraints to make sure that they are held
-- as linked lists correctly including self-referencing constraints.
--
box.execute("CREATE TABLE child (id INT PRIMARY KEY, a INT);")
box.execute("CREATE TABLE parent (a INT, id INT PRIMARY KEY);")

parent_id = box.space._space.index.name:select('PARENT')[1]['id']
child_id = box.space._space.index.name:select('CHILD')[1]['id']

t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {0}, {1}}
t = box.space._fk_constraint:insert(t)
t = {'fk_2', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {0}, {1}}
t = box.space._fk_constraint:insert(t)
t = {'fk_3', parent_id, child_id, false, 'simple', 'restrict', 'restrict', {1}, {0}}
t = box.space._fk_constraint:insert(t)
t = {'self_1', child_id, child_id, false, 'simple', 'restrict', 'restrict', {0}, {0}}
t = box.space._fk_constraint:insert(t)
t = {'self_2', parent_id, parent_id, false, 'simple', 'restrict', 'restrict', {1}, {1}}
t = box.space._fk_constraint:insert(t)

box.space._fk_constraint:count()
box.space._fk_constraint:delete{'fk_2', child_id}
box.space._fk_constraint:delete{'fk_1', child_id}
t = {'fk_2', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {0}, {1}}
t = box.space._fk_constraint:insert(t)
box.space._fk_constraint:delete{'fk_2', child_id}
box.space._fk_constraint:delete{'self_2', parent_id}
box.space._fk_constraint:delete{'self_1', child_id}
box.space._fk_constraint:delete{'fk_3', parent_id}
box.space._fk_constraint:count()

-- Replace is also OK.
--
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {0}, {1}}
t = box.space._fk_constraint:insert(t)
t = {'fk_1', child_id, parent_id, false, 'simple', 'cascade', 'restrict', {0}, {1}}
t = box.space._fk_constraint:replace(t)
box.space._fk_constraint:select({'fk_1', child_id})[1]['on_delete']
t = {'fk_1', child_id, parent_id, true, 'simple', 'cascade', 'restrict', {0}, {1}}
t = box.space._fk_constraint:replace(t)
box.space._fk_constraint:select({'fk_1', child_id})[1]['is_deferred']

box.space.CHILD:drop()
box.space.PARENT:drop()

-- Check that parser correctly handles MATCH, ON DELETE and
-- ON UPDATE clauses.
--
box.execute('CREATE TABLE tp (id INT PRIMARY KEY, a INT UNIQUE)')
box.execute('CREATE TABLE tc (id INT PRIMARY KEY, a INT REFERENCES tp(a) MATCH FULL ON DELETE SET NULL)')
box.execute('ALTER TABLE tc ADD CONSTRAINT fk1 FOREIGN KEY (id) REFERENCES tp(id) MATCH PARTIAL ON DELETE CASCADE ON UPDATE SET NULL')
-- Test that ADD/DROP CONSTRAINT return correct row_count value.
--
box.execute('SELECT row_count();')
box.space._fk_constraint:select{}
box.execute('ALTER TABLE tc DROP CONSTRAINT fk1;')
box.execute('SELECT row_count();')
box.execute('DROP TABLE tc')
box.execute('DROP TABLE tp')

-- gh-3475: ON UPDATE and ON DELETE clauses must appear once;
-- MATCH clause must come first.
box.execute('CREATE TABLE t1 (id INT PRIMARY KEY);')
box.execute('CREATE TABLE t2 (id INT PRIMARY KEY REFERENCES t2 ON DELETE CASCADE ON DELETE RESTRICT);')
box.execute('CREATE TABLE t2 (id INT PRIMARY KEY REFERENCES t2 ON DELETE CASCADE ON DELETE CASCADE);')
box.execute('CREATE TABLE t2 (id INT PRIMARY KEY REFERENCES t2 ON DELETE CASCADE ON UPDATE RESTRICT ON DELETE RESTRICT);')
box.execute('CREATE TABLE t2 (id INT PRIMARY KEY REFERENCES t2 ON DELETE CASCADE MATCH FULL);')
box.space.T1:drop()

-- Make sure that space:drop() works fine on self-referenced spaces.
--
box.execute("CREATE TABLE t4 (id INT PRIMARY KEY REFERENCES t4);")
box.space.T4:drop()

-- Make sure that child space can feature no PK.
--
t1 = box.schema.create_space("T1", {format = {'ID'}})
t2 = box.schema.create_space("T2")
i1 = box.space.T2:create_index('I1')
box.execute("ALTER TABLE t1 ADD CONSTRAINT fk FOREIGN KEY (id) REFERENCES t2;")

-- Make sure that if referenced columns (of parent space) are
-- ommitted and parent space doesn't have PK, then error is raised.
--
box.execute("ALTER TABLE t2 ADD CONSTRAINT fk FOREIGN KEY (id) REFERENCES t1;")

t1:drop()
t2:drop()

-- gh-4495: space alter resulted in foreign key mask reset.
-- Which in turn led to wrong byte-code generation. Make sure
-- that alter of space doesn't affect result of query execution.
--
box.execute("CREATE TABLE t (id TEXT PRIMARY KEY, a INTEGER NOT NULL);")
box.execute("CREATE TABLE s (t_id TEXT PRIMARY KEY, a INTEGER NOT NULL, FOREIGN KEY(t_id) REFERENCES t(id) ON DELETE CASCADE);")
box.space.T:insert({'abc', 1})
box.space.S:insert({'abc', 1})
box.execute("CREATE INDEX i ON s (t_id);")
box.execute("DELETE FROM t WHERE id = 'abc';")
box.space.T:select()

box.space.S:drop()
box.space.T:drop()

--
-- gh-3075: Check the auto naming of FOREIGN KEY constraints in
-- <ALTER TABLE ADD COLUMN>.
--
box.execute("CREATE TABLE t1 (a INT PRIMARY KEY)")
box.execute("CREATE TABLE check_naming (a INT PRIMARY KEY REFERENCES t1(a))")
box.execute("ALTER TABLE check_naming ADD b INT REFERENCES t1(a)")
box.execute("ALTER TABLE check_naming DROP CONSTRAINT \"fk_unnamed_CHECK_NAMING_2\"")
box.execute("DROP TABLE check_naming")
box.execute("DROP TABLE t1")

--- Clean-up SQL DD hash.
-test_run:cmd('restart server default with cleanup=1')
