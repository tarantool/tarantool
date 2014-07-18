-- console.lua -- internal file

local internal = require('console')
local formatter = require('yaml')
local session = require('session')

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

local function local_eval(self, line)
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
    return format(pcall(fun))
end

local function eval(line)
    return local_eval(nil, line)
end

local local_mt = {
    __index = {
        eval = local_eval;
        close = function() end;
        prompt = function() return 'tarantool' end;
    }
}

local function remote_eval(self, line)
    --
    -- call remote 'console.eval' function using 'dostring' and return result
    --
    local status, res = pcall(self.conn.call, self.conn, "dostring",
        "return require('console').eval(...)", line)
    if not status then
        -- remote request failed
        return format(status, res)
    end
    -- return formatted output from remote
    return res[1][1]
end

local function remote_close(self)
    pcall(self.conn.close, self.conn) -- ignore errors
end

local function remote_prompt(self)
    return string.format("%s:%s", self.conn.host, self.conn.port)
end

local remote_mt = {
    __index = {
        eval = remote_eval;
        close = remote_close;
        prompt = remote_prompt;
    }
}

local function read(host)
    local delim = session.delimiter()
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
    if session.storage.console ~= nil then
        return -- REPL is already enabled
    end
    session.storage.console = {}
    local handlers = {}
    table.insert(handlers, setmetatable({}, local_mt))
    session.storage.console.handlers = handlers
    local line
    while #handlers > 0 do
        local handler = handlers[#handlers]
        -- read
        local line = read(handler:prompt())
        if line == nil then
            -- remove handler
            io.write("\n")
            handler:close()
            table.remove(handlers)
        else
            -- eval
            local out = handler:eval(line)
            -- print
            io.write(out)
            internal.add_history(line)
        end
    end
end

local function connect(...)
    if not session.storage.console then
        error("console.connect() works only in interactive mode")
    end
    local conn = require('net.box'):new(...)
    conn:ping() -- test connection
    table.insert(session.storage.console.handlers, setmetatable(
        { conn =  conn}, remote_mt))
end

return {
    repl = repl;
    eval = eval;
    connect = connect;
}
