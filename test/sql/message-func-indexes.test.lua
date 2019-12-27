test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- Creating tables.
box.execute("CREATE TABLE t1(id INTEGER PRIMARY KEY, a INTEGER)")
box.execute("CREATE TABLE t2(object INTEGER PRIMARY KEY, price INTEGER, count INTEGER)")

-- Expressions that're supposed to create functional indexes
-- should return certain message.
box.execute("CREATE INDEX i1 ON t1(a+1)")
box.execute("CREATE INDEX i2 ON t1(a)")
box.execute("CREATE INDEX i3 ON t2(price + 100)")
box.execute("CREATE INDEX i4 ON t2(price)")
box.execute("CREATE INDEX i5 ON t2(count + 1)")
box.execute("CREATE INDEX i6 ON t2(count * price)")

-- Cleaning up.
box.execute("DROP TABLE t1")
box.execute("DROP TABLE t2")
