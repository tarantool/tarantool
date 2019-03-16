test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

-- Verify that constraints on 'view' option are working.

-- box.cfg()

-- Create space and view.
box.execute("CREATE TABLE t1(a INT, b INT, PRIMARY KEY(a, b));");
box.execute("CREATE VIEW v1 AS SELECT a+b FROM t1;");

-- View can't have any indexes.
box.execute("CREATE INDEX i1 on v1(a);");
v1 = box.space.V1;
v1:create_index('primary', {parts = {1, 'string'}})
v1:create_index('secondary', {parts = {1, 'string'}})

-- View option can't be changed.
v1 = box.space._space.index[2]:select('V1')[1]:totable();
v1[6]['view'] = false;
box.space._space:replace(v1);

t1 = box.space._space.index[2]:select('T1')[1]:totable();
t1[6]['view'] = true;
t1[6]['sql'] = 'SELECT * FROM t1;'
box.space._space:replace(t1);

-- View can't exist without SQL statement.
v1[6] = {};
v1[6]['view'] = true;
box.space._space:replace(v1);

-- Views can't be created via space_create().
box.schema.create_space('view', {view = true})

-- Space referenced by a view can't be renamed.
box.execute("ALTER TABLE t1 RENAME TO new_name;")

-- View can be created via straight insertion into _space.
sp = box.schema.create_space('test');
raw_sp = box.space._space:get(sp.id):totable();
sp:drop();
raw_sp[6].sql = 'CREATE VIEW v as SELECT * FROM t1;';
raw_sp[6].view = true;
sp = box.space._space:replace(raw_sp);
box.space._space:select(sp['id'])[1]['name']

-- Can't create view with incorrect SELECT statement.
box.space.test:drop();
-- This case must fail since parser converts it to expr AST.
raw_sp[6].sql = 'SELECT 1;';
sp = box.space._space:replace(raw_sp);

-- Can't drop space via Lua if at least one view refers to it.
box.execute('CREATE TABLE t2(id INT PRIMARY KEY);');
box.execute('CREATE VIEW v2 AS SELECT * FROM t2;');
box.space.T2:drop();
box.execute('DROP VIEW v2;');
box.execute('DROP TABLE t2;');

-- Check that alter transfers reference counter.
box.execute("CREATE TABLE t2(id INTEGER PRIMARY KEY);");
box.execute("CREATE VIEW v2 AS SELECT * FROM t2;");
box.execute("DROP TABLE t2;");
sp = box.space._space:get{box.space.T2.id};
sp = box.space._space:replace(sp);
box.execute("DROP TABLE t2;");
box.execute("DROP VIEW v2;");
box.execute("DROP TABLE t2;");

-- gh-3849: failed to create VIEW in form of AS VALUES (const);
--
box.execute("CREATE VIEW cv AS VALUES(1);")
box.execute("CREATE VIEW cv1 AS VALUES('k', 1);")
box.execute("CREATE VIEW cv2 AS VALUES((VALUES((SELECT 1))));")
box.execute("CREATE VIEW cv3 AS VALUES(1+2, 1+2);")
box.execute("DROP VIEW cv;")
box.execute("DROP VIEW cv1;")
box.execute("DROP VIEW cv2;")
box.execute("DROP VIEW cv3;")

-- gh-3815: AS VALUES syntax didn't incerement VIEW reference
-- counter. Moreover, tables within sub-select were not accounted
-- as well.
--
box.execute("CREATE TABLE b (s1 INT PRIMARY KEY);")
box.execute("CREATE VIEW bv (wombat) AS VALUES ((SELECT 'k' FROM b));")
box.execute("DROP TABLE b;")
box.execute("DROP VIEW bv;")
box.execute("DROP TABLE b;")

box.execute("CREATE TABLE b (s1 INT PRIMARY KEY);")
box.execute("CREATE TABLE c (s1 INT PRIMARY KEY);")
box.execute("CREATE VIEW bcv AS SELECT * FROM b WHERE s1 IN (SELECT * FROM c);")
box.execute("DROP TABLE c;")
box.execute("DROP VIEW bcv;")
box.execute("DROP TABLE c;")

box.execute("CREATE TABLE c (s1 INT PRIMARY KEY);")
box.execute("CREATE VIEW bcv(x, y) AS VALUES((SELECT 'k' FROM b), (VALUES((SELECT 1 FROM b WHERE s1 IN (VALUES((SELECT 1 + c.s1 FROM c)))))))")
box.execute("DROP TABLE c;")
box.space.BCV:drop()
box.execute("DROP TABLE c;")
box.execute("DROP TABLE b;")

-- gh-3814: make sure that recovery of view processed without
-- unexpected errors.
--
box.snapshot()
box.execute("CREATE TABLE t2 (id INT PRIMARY KEY);")
box.execute("CREATE VIEW v2 AS SELECT * FROM t2;")
test_run:cmd('restart server default')

box.execute("DROP TABLE t2;")
box.execute("SELECT * FROM v2;")
box.space.V2:drop()
box.space.T2:drop()

-- Cleanup
box.execute("DROP VIEW v1;");
box.execute("DROP TABLE t1;");
