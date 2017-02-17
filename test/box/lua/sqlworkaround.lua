local module = {}

local ffi = require "ffi"
ffi.cdef[[
    int sql_schema_put(int, int, const char**);
    void free(void *);
]]

-- Manually feed in data in sqlite_master row format.
-- Populate schema objects, make it possible to query
-- Tarantool spaces with SQL.
function module.sql_schema_put(idb, ...)
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

function module.sql_pageno(space_id, index_id)
    return space_id * 32 + index_id
end

return module