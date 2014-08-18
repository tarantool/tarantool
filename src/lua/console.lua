-- console.lua -- internal file

local internal = require('console')
local formatter = require('yaml')
local fiber = require('fiber')
local socket = require('socket')
local log = require('log')
local errno = require('errno')

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

--
-- Evaluate command on local server
--
local function local_eval(self, line)
    if not line then
        return nil
    end
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

--
-- Evaluate command on remote server
--
local function remote_eval(self, line)
    if not line then
        pcall(self.remote.close, self.remote)
        self.remote = nil
        self.eval = nil
        self.prompt = nil
        return ""
    end
    --
    -- call remote 'console.eval' function using 'dostring' and return result
    --
    local status, res = pcall(self.remote.call, self.remote, "dostring",
        "return require('console').eval(...)", line)
    if not status then
        -- remote request failed
        return format(status, res)
    end
    -- return formatted output from remote
    return res[1][1]
end

--
-- Read command from stdin
--
local function local_read(self)
    local buf = ""
    local prompt = self.prompt
    while true do
        local delim = self.delimiter
        local line = internal.readline(prompt.. "> ")
        if not line then
            return nil
        end
        buf = buf..line
        if #buf >= #delim and buf:sub(#buf - #delim + 1) == delim then
            buf = buf:sub(0, #buf - #delim)
            break
        end
        buf = buf.."\n"
        prompt = string.rep(' ', #self.prompt)
    end
    internal.add_history(buf)
    return buf
end

--
-- Print result to stdout
--
local function local_print(self, output)
    if output == nil then
        self.running = nil
        return
    end
    print(output)
end

--
-- Read command from connected client console.listen()
--
local function client_read(self)
    local delim = self.delimiter.."\n"
    local buf = self.client:read(delim)
    if buf == nil then
        return nil
    elseif buf == "" then
        return nil -- EOF
    elseif buf == "~.\n" then
        -- Escape sequence to close current connection (like SSH)
        return nil
    end
    -- remove trailing delimiter
    return buf:match("^(.*)"..delim)
end

--
-- Print result to connected client from console.listen()
--
local function client_print(self, output)
    if not self.client then
        return
    elseif not output then
        -- disconnect peer
        local peer = self.client:peer()
        log.info("console: client %s:%s disconnected", peer.host, peer.port)
        self.client:shutdown()
        self.client:close()
        self.client = nil
        self.running = nil
        return
    end
    self.client:write(output)
end

--
-- REPL state
--
local repl_mt = {
    __index = {
        running = false;
        delimiter = "";
        prompt = "tarantool";
        read = local_read;
        eval = local_eval;
        print = local_print;
    };
}

--
-- REPL = read-eval-print-loop
--
local function repl(self)
    fiber.self().storage.console = self
    while self.running do
        local command = self:read()
        local output = self:eval(command)
        self:print(output)
    end
    fiber.self().storage.console = nil
end

--
-- Set delimiter
--
local function delimiter(delim)
    local self = fiber.self().storage.console
    if self == nil then
        error("console.delimiter(): need existing console")
    end
    if delim == nil then
        return self.delimiter
    elseif type(delim) == 'string' then
        self.delimiter = delim
    else
        error('invalid delimiter')
    end
end

--
-- Start REPL on stdin
--
local started = false
local function start()
    if started then
        error("console is already started")
    end
    started = true
    local self = setmetatable({ running = true }, repl_mt)
    repl(self)
    started = false
end

--
-- Connect to remove server
--
local function connect(...)
    local self = fiber.self().storage.console
    if self == nil then
        error("console.connect() need existing console")
    end
    -- connect to remote host
    local remote = require('net.box'):new(...)
    -- check permissions
    remote:call('dostring', 'return true')
    -- override methods
    self.remote = remote
    self.eval = remote_eval
    self.prompt = string.format("%s:%s", self.remote.host, self.remote.port)
    log.info("connected to %s:%s", self.remote.host, self.remote.port)
end

local function server_loop(server)
    while server:readable() do
        local client = server:accept()
        if client then
            local peer = client:peer()
            log.info("console: client %s:%s connected", peer.host, peer.port)
            local state = setmetatable({
                running = true;
                read = client_read;
                print = client_print;
                client = client;
            }, repl_mt)
            fiber.create(repl, state)
        end
    end
    log.info("console: stopped")
end

--
-- Start admin server
--
local function listen(uri)
    local host, port
    if uri == nil then
        host = 'unix/'
    elseif type(uri) == 'number' or uri:match("^%d+$") then
        port = tonumber(uri)
    elseif uri:match("^/") then
        host = 'unix/'
        port = uri
    else
        host, port = uri:match("^(.*):(.*)$")
        if not host then
            port = uri
        end
    end
    local server
    if host == 'unix/' then
        port = port or '/tmp/tarantool-console.sock'
        os.remove(port)
        server = socket('AF_UNIX', 'SOCK_STREAM', 0)
    else
        host = host or '127.0.0.1'
        port = port or 3313
        server = socket('AF_INET', 'SOCK_STREAM', 'tcp')
    end
    if not server then
	error(string.format('failed to create socket %s%s : %s',
			    host, port, errno.strerror()))
    end
    server:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)

    if not server:bind(host, port) then
        local msg = string.format('failed to bind: %s', server:error())
        server:close()
        error(msg)
    end
    if not server:listen() then
        local msg = string.format('failed to listen: %s', server:error())
        server:close()
        error(msg)
    end
    log.info("console: started on %s:%s", host, port)
    fiber.create(server_loop, server)
    return server
end

return {
    start = start;
    eval = eval;
    delimiter = delimiter;
    connect = connect;
    listen = listen;
}
