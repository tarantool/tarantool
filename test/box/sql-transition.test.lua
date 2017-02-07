test_run = require('test_run').new()

test_run:cmd("setopt delimiter ';;'")

ffi = require "ffi"
ffi.cdef[[
    int sql_schema_put(int, int, const char**);
    void free(void *);
]]

-- Manually feed in data in sqlite_master row format.
-- Populate schema objects, make it possible to query
-- Tarantool spaces with SQL.
function sql_schema_put(idb, ...)
    local argc = select('#', ...)
    local argv, cargv = {}, ffi.new('const char*[?]', argc+1)
    for i = 0,argc-1 do
        local v = tostring(select(i+1, ...))
        argv[i] = v
        cargv[i] = v
    end
    cargv[argc] = nil
    local rc = ffi.C.sql_schema_put(idb, argc, cargv);
    local err_msg
    if cargv[0] ~= nil then
        err_msg = ffi.string(cargv[0])
        ffi.C.free(ffi.cast('void *', cargv[0]))
    end
    return rc, err_msg
end

function sql_pageno(space_id, index_id)
    return space_id * 32 + index_id
end

test_run:cmd("setopt delimiter ''");;

-- test invalid input
sql_schema_put(0, "invalid", 1, "CREATE FROB")

-- create space
foobar = box.schema.space.create("foobar")
_ = foobar:create_index("primary",{parts={1,"number"}})

foobar_pageno = sql_pageno(foobar.id, foobar.index.primary.id)
foobar_sql = "CREATE TABLE foobar (foo PRIMARY KEY, bar) WITHOUT ROWID"
sql_schema_put(0, "foobar", foobar_pageno, foobar_sql)
sql_schema_put(0, "sqlite_autoindex_foobar_1", foobar_pageno, "")

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
