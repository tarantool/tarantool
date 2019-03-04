local ffi = require('ffi')
local fun = require('fun')
local merger = require('merger')

local ibuf_t = ffi.typeof('struct ibuf')
local merge_source_t = ffi.typeof('struct merge_source')

-- Create a source from one buffer.
merger.new_source_frombuffer = function(buf)
    local func_name = 'merger.new_source_frombuffer'
    if type(buf) ~= 'cdata' or not ffi.istype(ibuf_t, buf) then
        error(('Usage: %s(<cdata<struct ibuf>>)'):format(func_name), 0)
    end

    return merger.new_buffer_source(fun.iter({buf}))
end

-- Create a source from one table.
merger.new_source_fromtable = function(tbl)
    local func_name = 'merger.new_source_fromtable'
    if type(tbl) ~= 'table' then
        error(('Usage: %s(<table>)'):format(func_name), 0)
    end

    return merger.new_table_source(fun.iter({tbl}))
end

local methods = {
    ['select'] = merger.internal.select,
    ['pairs']  = merger.internal.ipairs,
    ['ipairs']  = merger.internal.ipairs,
}

ffi.metatype(merge_source_t, {
    __index = function(self, key)
        return methods[key]
    end,
    -- Lua 5.2 compatibility
    __pairs = merger.internal.ipairs,
    __ipairs = merger.internal.ipairs,
})
