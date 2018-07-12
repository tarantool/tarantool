env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- create space
box.sql.execute("CREATE TABLE foobar (foo INT PRIMARY KEY, bar TEXT)")

-- prepare data
box.sql.execute("INSERT INTO foobar VALUES (1, 'foo')")
box.sql.execute("INSERT INTO foobar VALUES (2, 'bar')")
box.sql.execute("INSERT INTO foobar VALUES (1000, 'foobar')")

box.sql.execute("INSERT INTO foobar VALUES (1, 'duplicate')")

-- simple select
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar LIMIT 2")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo=2")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo>2")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo>=2")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo=10000")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo>10000")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo<2")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo<2.001")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo<=2")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo<100")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar WHERE bar='foo'")
box.sql.execute("SELECT count(*) FROM foobar")
box.sql.execute("SELECT count(*) FROM foobar WHERE bar='foo'")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar ORDER BY bar")
box.sql.execute("SELECT bar, foo, 42, 'awesome' FROM foobar ORDER BY bar DESC")

-- updates
box.sql.execute("REPLACE INTO foobar VALUES (1, 'cacodaemon')")
box.sql.execute("SELECT COUNT(*) FROM foobar WHERE foo=1")
box.sql.execute("SELECT COUNT(*) FROM foobar WHERE bar='cacodaemon'")
box.sql.execute("DELETE FROM foobar WHERE bar='cacodaemon'")
box.sql.execute("SELECT COUNT(*) FROM foobar WHERE bar='cacodaemon'")

-- multi-index

-- create space
box.sql.execute("CREATE TABLE barfoo (bar TEXT, foo NUM PRIMARY KEY)")
box.sql.execute("CREATE UNIQUE INDEX barfoo2 ON barfoo(bar)")

-- prepare data
box.sql.execute("INSERT INTO barfoo VALUES ('foo', 1)")
box.sql.execute("INSERT INTO barfoo VALUES ('bar', 2)")
box.sql.execute("INSERT INTO barfoo VALUES ('foobar', 1000)")

-- create a trigger
box.sql.execute("CREATE TRIGGER tfoobar AFTER INSERT ON foobar BEGIN INSERT INTO barfoo VALUES ('trigger test', 9999); END")
box.sql.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"");

-- Many entries
box.sql.execute("CREATE TABLE t1(a INT,b INT,c INT,PRIMARY KEY(b,c));")
box.sql.execute("WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<1000) INSERT INTO t1 SELECT x, x%40, x/40 FROM cnt;")
box.sql.execute("SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;");

test_run:cmd('restart server default');

-- prove that trigger survived
box.sql.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"");

-- ... functional
box.sql.execute("INSERT INTO foobar VALUES ('foobar trigger test', 8888)")
box.sql.execute("SELECT * FROM barfoo WHERE foo = 9999");

-- and still persistent
box.sql.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"")

-- and can be dropped just once
box.sql.execute("DROP TRIGGER tfoobar")
-- Should error
box.sql.execute("DROP TRIGGER tfoobar")
-- Should be empty
box.sql.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"")

-- prove barfoo2 still exists
box.sql.execute("INSERT INTO barfoo VALUES ('xfoo', 1)")

box.sql.execute("SELECT * FROM barfoo")
box.sql.execute("SELECT * FROM foobar");
box.sql.execute("SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;");

-- cleanup
box.sql.execute("DROP TABLE foobar")
box.sql.execute("DROP TABLE barfoo")
box.sql.execute("DROP TABLE t1")
