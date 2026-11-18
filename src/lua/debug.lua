---The `debug` library provides an interface for debugging Lua programs.
---Tarantool adds `debug.sourcedir()`, `debug.sourcefile()` and the
---`debug.__dir__` / `debug.__file__` shortcuts.
---
---**Example:**
---
---```lua
---print(debug.sourcedir())
---print(debug.sourcefile())
---print(debug.traceback())
---```
---
---@class debug
---@field sourcefile fun(level?: integer): string? Return the source file
---    name.
---@field sourcedir fun(level?: integer): string? Return the source
---    directory name.
---@field __file__ string? The source file name.
---@field __dir__ string? The source directory name.

local debug = require('debug')

local function sourcefile(level)
    local info = debug.getinfo(level or 2, 'S')
    local source = info and info.source
    return source and source:startswith('@') and source:sub(2) or nil
end

local function sourcedir(level)
    local source = sourcefile(level and level + 1 or 3)
    return source and source:match('(.*)/') or '.'
end

setmetatable(debug, {
    __index = function(self, v)
        -- Be careful editing these tail calls,
        -- since it may affect function results
        if v == '__file__' then
            return sourcefile()
        elseif v == '__dir__' then
            return sourcedir()
        end
        return rawget(self, v)
    end
})

debug.sourcefile = sourcefile
debug.sourcedir = sourcedir
