test_run = require('test_run').new()
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

-- prove barfoo2 was created
box.execute("INSERT INTO barfoo VALUES ('xfoo', 1)")

box.execute("SELECT foo, bar FROM barfoo")
box.execute("SELECT foo, bar FROM barfoo WHERE foo==2")
box.execute("SELECT foo, bar FROM barfoo WHERE bar=='foobar'")
box.execute("SELECT foo, bar FROM barfoo WHERE foo>=2")
box.execute("SELECT foo, bar FROM barfoo WHERE foo<=2")

-- cleanup
box.execute("DROP INDEX barfoo2 ON barfoo")
box.execute("DROP TABLE foobar")
box.execute("DROP TABLE barfoo")

-- attempt to create a table lacking PRIMARY KEY
box.execute("CREATE TABLE without_rowid_lacking_primary_key(x SCALAR)")

-- create a table with implicit indices (used to SEGFAULT)
box.execute("CREATE TABLE implicit_indices(a INT PRIMARY KEY,b INT,c INT,d TEXT UNIQUE)")
box.execute("DROP TABLE implicit_indices")
