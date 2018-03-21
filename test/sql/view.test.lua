test_run = require('test_run').new()

-- Verify that constraints on 'view' option are working.

-- box.cfg()

-- Create space and view.
box.sql.execute("CREATE TABLE t1(a, b, PRIMARY KEY(a, b));");
box.sql.execute("CREATE VIEW v1 AS SELECT a+b FROM t1;");

-- View can't have any indexes.
box.sql.execute("CREATE INDEX i1 on v1(a);");
v1 = box.space.V1;
v1:create_index('primary', {parts = {1, 'string'}})
v1:create_index('secondary', {parts = {1, 'string'}})

-- View option can't be changed.
v1 = box.space._space.index[2]:select('V1')[1]:totable();
v1[6]['view'] = false;
box.space._space:replace(v1);

t1 = box.space._space.index[2]:select('T1')[1]:totable();
t1[6]['view'] = true;
box.space._space:replace(t1);

-- View can't exist without SQL statement.
v1[6] = {};
v1[6]['view'] = true;
box.space._space:replace(v1);

-- Views can't be created via space_create().
box.schema.create_space('view', {view = true})

-- View can be created via straight insertion into _space.
sp = box.schema.create_space('test');
raw_sp = box.space._space:get(sp.id):totable();
sp:drop();
raw_sp[6].sql = 'fake';
raw_sp[6].view = true;
sp = box.space._space:replace(raw_sp);
box.space._space:select(sp['id'])[1]['name']

-- Cleanup
box.space.test:drop();
box.sql.execute("DROP TABLE t1;");
box.sql.execute("DROP VIEW v1;");
