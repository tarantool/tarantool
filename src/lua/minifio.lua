local ffi = require('ffi')
local minifio = require('internal.minifio')

-- Several file manipulation functions without dependencies on
-- tarantool's built-in modules to use at early initialization
-- stage.

ffi.cdef([[
    char *
    dirname(char *path);
]])

-- {{{ Functions exposed by fio

-- NB: minifio.cwd() is defined in src/lua/minifio.c.

function minifio.pathjoin(...)
    local i, path = 1, nil

    local len = select('#', ...)
    while i <= len do
        local sp = select(i, ...)
        if sp == nil then
            error("fio.pathjoin(): undefined path part "..i, 0)
        end

        sp = tostring(sp)
        if sp ~= '' then
            path = sp
            break
        else
            i = i + 1
        end
    end

    if path == nil then
        return '.'
    end

    i = i + 1
    while i <= len do
        local sp = select(i, ...)
        if sp == nil then
            error("fio.pathjoin(): undefined path part "..i, 0)
        end

        sp = tostring(sp)
        if sp ~= '' then
            path = path .. '/' .. sp
        end

        i = i + 1
    end

    path = path:gsub('/+', '/')
    if path ~= '/' then
        path = path:gsub('/$', '')
    end

    return path
end

function minifio.abspath(path)
    if path == nil then
        error("Usage: fio.abspath(path)", 0)
    end
    path = path
    local joined_path
    local path_tab = {}
    if string.sub(path, 1, 1) == '/' then
        joined_path = path
    else
        joined_path = minifio.pathjoin(minifio.cwd(), path)
    end
    for sp in string.gmatch(joined_path, '[^/]+') do
        if sp == '..' then
            table.remove(path_tab)
        elseif sp ~= '.' then
            table.insert(path_tab, sp)
        end
    end
    return '/' .. table.concat(path_tab, '/')
end

-- }}} Functions exposed by fio

-- {{{ Functions replaced by fio

-- Similar to fio.dirname, but it doesn't use
-- cord_ibuf_take()/cord_ibuf_put().
function minifio.dirname(path)
    if type(path) ~= 'string' then
        error("Usage: minifio.dirname(path)", 0)
    end
    -- Can't just cast path to char * - on Linux dirname modifies
    -- its argument.
    local bsize = #path + 1
    local buf = ffi.new('char[?]', bsize)
    ffi.copy(buf, ffi.cast('const char *', path), bsize)
    return ffi.string(ffi.C.dirname(buf))
end

-- }}} Functions replaced by fio

-- Functions to expose from fio as is. List them separately to
-- reduce probability of a mistake.
minifio.expose_from_fio = {
    cwd = minifio.cwd,
    pathjoin = minifio.pathjoin,
    abspath = minifio.abspath,
}

return minifio
