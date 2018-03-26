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

-- prove barfoo2 was created
box.sql.execute("INSERT INTO barfoo VALUES ('xfoo', 1)")

box.sql.execute("SELECT foo, bar FROM barfoo")
box.sql.execute("SELECT foo, bar FROM barfoo WHERE foo==2")
box.sql.execute("SELECT foo, bar FROM barfoo WHERE bar=='foobar'")
box.sql.execute("SELECT foo, bar FROM barfoo WHERE foo>=2")
box.sql.execute("SELECT foo, bar FROM barfoo WHERE foo<=2")

-- cleanup
box.sql.execute("DROP INDEX barfoo2 ON barfoo")
box.sql.execute("DROP TABLE foobar")
box.sql.execute("DROP TABLE barfoo")

-- attempt to create a table lacking PRIMARY KEY
box.sql.execute("CREATE TABLE without_rowid_lacking_primary_key(x)")

-- attempt to create a table lacking WITHOUT ROWID clause
box.sql.execute("CREATE TABLE rowid(x)")

-- create a table with implicit indices (used to SEGFAULT)
box.sql.execute("CREATE TABLE implicit_indices(a PRIMARY KEY,b,c,d UNIQUE)")
box.sql.execute("DROP TABLE implicit_indices")
