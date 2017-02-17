wa = require 'sqlworkaround'

test_run = require('test_run').new()

-- test invalid input
wa.sql_schema_put(0, "invalid", 1, "CREATE FROB")

-- create space
foobar = box.schema.space.create("foobar")
_ = foobar:create_index("primary",{parts={1,"number"}})

foobar_pageno = wa.sql_pageno(foobar.id, foobar.index.primary.id)
foobar_sql = "CREATE TABLE foobar (foo PRIMARY KEY, bar) WITHOUT ROWID"
wa.sql_schema_put(0, "foobar", foobar_pageno, foobar_sql)
wa.sql_schema_put(0, "sqlite_autoindex_foobar_1", foobar_pageno, "")

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

-- cleanup
foobar:drop()

-- multi-index

-- create space
barfoo = box.schema.space.create("barfoo")
_ = barfoo:create_index("primary",{parts={2,"number"}})
_ = barfoo:create_index("secondary",{parts={1,"string"}})

barfoo_pageno = wa.sql_pageno(barfoo.id, barfoo.index.primary.id)
barfoo2_pageno = wa.sql_pageno(barfoo.id, barfoo.index.secondary.id)
barfoo_sql = "CREATE TABLE barfoo (bar, foo PRIMARY KEY) WITHOUT ROWID"
wa.sql_schema_put(0, "barfoo", barfoo_pageno, barfoo_sql)
wa.sql_schema_put(0, "sqlite_autoindex_barfoo_1", barfoo_pageno, "")
wa.sql_schema_put(0, "barfoo2", barfoo2_pageno, "CREATE INDEX barfoo2 ON barfoo(bar)")

-- prepare data
barfoo:insert({'foo', 1})
barfoo:insert({'bar', 2})
barfoo:insert({'foobar', 1000})

box.sql.execute("SELECT foo, bar FROM barfoo")
box.sql.execute("SELECT foo, bar FROM barfoo WHERE foo==2")
box.sql.execute("SELECT foo, bar FROM barfoo WHERE bar=='foobar'")
box.sql.execute("SELECT foo, bar FROM barfoo WHERE foo>=2")
box.sql.execute("SELECT foo, bar FROM barfoo WHERE foo<=2")

-- cleanup
barfoo:drop()
