wa = require 'sqlworkaround'

test_run = require('test_run').new()

-- box.cfg()

-- create space
zoobar = box.schema.space.create("zoobar")
_ = zoobar:create_index("primary",{parts={2,"number"}})
_ = zoobar:create_index("secondary", {parts={1, 'number',  4, 'number'}})

zoobar_pageno =  wa.sql_pageno(zoobar.id, zoobar.index.primary.id)
zoobar2_pageno = wa.sql_pageno(zoobar.id, zoobar.index.secondary.id)

wa.sql_schema_put(0, "zoobar"                   , zoobar_pageno , "CREATE TABLE zoobar (c1, c2 PRIMARY KEY, c3, c4) WITHOUT ROWID")
wa.sql_schema_put(0, "sqlite_autoindex_zoobar_1", zoobar_pageno , "")
wa.sql_schema_put(0, "zoobar2"                  , zoobar2_pageno, "CREATE UNIQUE INDEX zoobar2 ON zoobar(c1, c4)")

-- Debug
-- box.sql.execute("PRAGMA vdbe_debug=ON ; INSERT INTO zoobar VALUES (111, 222, 'c3', 444)")

-- Seed entry
box.sql.execute("INSERT INTO zoobar VALUES (111, 222, 'c3', 444)")

-- PK must be unique
box.sql.execute("INSERT INTO zoobar VALUES (112, 222, 'c3', 444)")

-- Unique index must be respected
box.sql.execute("INSERT INTO zoobar VALUES (111, 223, 'c3', 444)")

-- Cleanup
zoobar:drop()

-- Debug
-- require("console").start()
