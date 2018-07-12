env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default with cleanup=1')


-- Check that tuple inserted into _fk_constraint is FK constrains
-- valid data.
--
box.sql.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a INT, b INT);")
box.sql.execute("CREATE UNIQUE INDEX i1 ON t1(a);")
box.sql.execute("CREATE TABLE t2 (a INT, b INT, id INT PRIMARY KEY);")
box.sql.execute("CREATE VIEW v1 AS SELECT * FROM t1;")

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
box.sql.execute("DROP VIEW v1;")

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
box.sql.execute("INSERT INTO t2 VALUES (1, 2, 3);")
t = {'fk_1', child_id, parent_id, false, 'simple', 'restrict', 'restrict', {2}, {1}}
box.space._fk_constraint:insert(t)
box.sql.execute("DELETE FROM t2;")

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
box.sql.execute("DROP INDEX i1 on t1;")

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
box.sql.execute("CREATE TABLE child (id INT PRIMARY KEY, a INT);")
box.sql.execute("CREATE TABLE parent (a INT, id INT PRIMARY KEY);")

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
box.sql.execute('CREATE TABLE tp (id INT PRIMARY KEY, a INT UNIQUE)')
box.sql.execute('CREATE TABLE tc (id INT PRIMARY KEY, a INT REFERENCES tp(a) ON DELETE SET NULL MATCH FULL)')
box.sql.execute('ALTER TABLE tc ADD CONSTRAINT fk1 FOREIGN KEY (id) REFERENCES tp(id) MATCH PARTIAL ON DELETE CASCADE ON UPDATE SET NULL')
box.space._fk_constraint:select{}
box.sql.execute('DROP TABLE tc')
box.sql.execute('DROP TABLE tp')

--- Clean-up SQL DD hash.
-test_run:cmd('restart server default with cleanup=1')
