env = require('test_run')
test_run = env.new()

-- create space
box.sql.execute("CREATE TABLE foobar (foo PRIMARY KEY, bar)")

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
box.sql.execute("CREATE TABLE barfoo (bar, foo NUM PRIMARY KEY)")
box.sql.execute("CREATE UNIQUE INDEX barfoo2 ON barfoo(bar)")

-- prepare data
box.sql.execute("INSERT INTO barfoo VALUES ('foo', 1)")
box.sql.execute("INSERT INTO barfoo VALUES ('bar', 2)")
box.sql.execute("INSERT INTO barfoo VALUES ('foobar', 1000)")

-- create a trigger
box.sql.execute("CREATE TRIGGER tfoobar AFTER INSERT ON foobar BEGIN INSERT INTO barfoo VALUES ('trigger test', 9999); END")
box.sql.execute("SELECT * FROM _trigger");

test_run:cmd('restart server default');

-- prove that trigger survived
box.sql.execute("SELECT * FROM _trigger");

-- ... functional
box.sql.execute("INSERT INTO foobar VALUES ('foobar trigger test', 8888)")
box.sql.execute("SELECT * FROM barfoo WHERE foo = 9999");

-- and still persistent
box.sql.execute("SELECT * FROM _trigger")

-- and can be dropped just once
box.sql.execute("DROP TRIGGER tfoobar")
-- Should error
box.sql.execute("DROP TRIGGER tfoobar")
-- Should be empty
box.sql.execute("SELECT * FROM _trigger")

-- prove barfoo2 still exists
box.sql.execute("INSERT INTO barfoo VALUES ('xfoo', 1)")

box.sql.execute("SELECT * FROM barfoo")
box.sql.execute("SELECT * FROM foobar");

-- cleanup
box.sql.execute("DROP TABLE foobar")
box.sql.execute("DROP TABLE barfoo")
