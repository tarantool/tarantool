-- console.lua -- internal file

local internal = require('console')
local formatter = require('yaml')

local function format(status, ...)
    -- When storing a nil in a Lua table, there is no way to
    -- distinguish nil value from no value. This is a trick to
    -- make sure yaml converter correctly
    local function wrapnull(v)
        return v == nil and formatter.NULL or v
    end
    if not status then
        local v = ...
        return formatter.encode({{error = wrapnull(v) }})
    end
    local count = select('#', ...)
    if count == 0 then
        return "---\n...\n"
    end
    local res = {}
    for i=1,count,1 do
        table.insert(res, wrapnull(select(i, ...)))
    end
    return formatter.encode(res)
end

local function eval(line, ...)
    --
    -- Attempt to append 'return ' before the chunk: if the chunk is
    -- an expression, this pushes results of the expression onto the
    -- stack. If the chunk is a statement, it won't compile. In that
    -- case try to run the original string.
    --
    local fun, errmsg = loadstring("return "..line)
    if not fun then
        fun, errmsg = loadstring(line)
    end
    if not fun then
        return format(false, errmsg)
    end
    return format(pcall(fun, ...))
end

local function read(host)
    local delim = require('session').delimiter()
    local linenum = 0
    local buf = ""
    while true do
        local prompt = (linenum == 0 and host or string.rep(' ', #host)).. "> "
        local line = internal.readline(prompt)
        if not line then
            return nil
        end
        buf = buf..line
        if #buf >= #delim and buf:sub(#buf - #delim + 1) == delim then
            return buf:sub(0, #buf - #delim)
        end
        buf = buf.."\n"
        linenum = linenum + 1
    end
end

local function repl()
    local host = "tarantool"
    local line
    while true do
        -- read
        local line = read(host)
        if line == nil then
            break
        end
        -- eval
        local out = eval(line)
        -- print
        io.write(out)
        internal.add_history(line)
    end
end

return {
    repl = repl;
    eval = eval;
}
