env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- create space
box.execute("CREATE TABLE foobar (foo INT PRIMARY KEY, bar TEXT)")

-- prepare data
box.execute("INSERT INTO foobar VALUES (1, 'foo')")
box.execute("INSERT INTO foobar VALUES (2, 'bar')")
box.execute("INSERT INTO foobar VALUES (1000, 'foobar')")

box.execute("INSERT INTO foobar VALUES (1, 'duplicate')")

-- simple select
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar LIMIT 2")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo=2")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo>2")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo>=2")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo=10000")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo>10000")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo<2")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo<2.001")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo<=2")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo<100")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE bar='foo'")
box.execute("SELECT count(*) FROM foobar")
box.execute("SELECT count(*) FROM foobar WHERE bar='foo'")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar ORDER BY bar")
box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar ORDER BY bar DESC")

-- updates
box.execute("REPLACE INTO foobar VALUES (1, 'cacodaemon')")
box.execute("SELECT COUNT(*) FROM foobar WHERE foo=1")
box.execute("SELECT COUNT(*) FROM foobar WHERE bar='cacodaemon'")
box.execute("DELETE FROM foobar WHERE bar='cacodaemon'")
box.execute("SELECT COUNT(*) FROM foobar WHERE bar='cacodaemon'")

-- multi-index

-- create space
box.execute("CREATE TABLE barfoo (bar TEXT, foo NUMBER PRIMARY KEY)")
box.execute("CREATE UNIQUE INDEX barfoo2 ON barfoo(bar)")

-- prepare data
box.execute("INSERT INTO barfoo VALUES ('foo', 1)")
box.execute("INSERT INTO barfoo VALUES ('bar', 2)")
box.execute("INSERT INTO barfoo VALUES ('foobar', 1000)")

-- create a trigger
box.execute("CREATE TRIGGER tfoobar AFTER INSERT ON foobar FOR EACH ROW BEGIN INSERT INTO barfoo VALUES ('trigger test', 9999); END")
box.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"");

-- Many entries
box.execute("CREATE TABLE t1(a INT,b INT,c INT,PRIMARY KEY(b,c));")
box.execute("WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<1000) INSERT INTO t1 SELECT x, x%40, x/40 FROM cnt;")
box.execute("SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;");

test_run:cmd('restart server default');

-- prove that trigger survived
box.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"");

-- ... functional
box.execute("INSERT INTO foobar VALUES ('foobar trigger test', 8888)")
box.execute("SELECT * FROM barfoo WHERE foo = 9999");

-- and still persistent
box.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"")

-- and can be dropped just once
box.execute("DROP TRIGGER tfoobar")
-- Should error
box.execute("DROP TRIGGER tfoobar")
-- Should be empty
box.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"")

-- prove barfoo2 still exists
box.execute("INSERT INTO barfoo VALUES ('xfoo', 1)")

box.execute("SELECT * FROM barfoo")
box.execute("SELECT * FROM foobar");
box.execute("SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;");

-- cleanup
box.execute("DROP TABLE foobar")
box.execute("DROP TABLE barfoo")
box.execute("DROP TABLE t1")
