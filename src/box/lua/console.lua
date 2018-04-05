-- console.lua -- internal file

local internal = require('console')
local session_internal = require('box.internal.session')
local fiber = require('fiber')
local socket = require('socket')
local log = require('log')
local errno = require('errno')
local urilib = require('uri')
local yaml = require('yaml')
local net_box = require('net.box')

local YAML_TERM = '\n...\n'

local function format(status, ...)
    local err
    if status then
        -- serializer can raise an exception
        status, err = pcall(internal.format, ...)
        if status then
            return err
        else
            err = 'console: an exception occurred when formatting the output: '..
                tostring(err)
        end
    else
        err = ...
        if err == nil then
            err = box.NULL
        end
    end
    return internal.format({ error = err })
end

--
-- Evaluate command on local instance
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

local text_connection_mt = {
    __index = {
        --
        -- Close underlying socket.
        --
        close = function(self)
            self._socket:close()
        end,
        --
        -- Write a text into a socket.
        -- @param test Text to send.
        -- @retval not nil Bytes sent.
        -- @retval     nil Error.
        --
        write = function(self, text)
            return self._socket:write(text)
        end,
        --
        -- Read a text from a socket until YAML terminator.
        -- @retval not nil Well formatted YAML.
        -- @retval     nil Error.
        --
        read = function(self)
            local ret = self._socket:read(YAML_TERM)
            if ret and ret ~= '' then
                return ret
            end
        end,
        --
        -- Write + Read.
        --
        eval = function(self, text)
            text = text..'$EOF$\n'
            if self:write(text) then
                local rc = self:read()
                if rc then
                    return rc
                end
            end
            error(self:set_error())
        end,
        --
        -- Make the connection be in error state, set error
        -- message.
        -- @retval Error message.
        --
        set_error = function(self)
            self.state = 'error'
            self.error = self._socket:error()
            if not self.error then
                self.error = 'Peer closed'
            end
            return self.error
        end,
    }
}

--
-- Wrap an existing socket with text Read-Write API inside a
-- netbox-like object.
-- @param connection Socket to wrap.
-- @param url Parsed destination URL.
-- @retval nil, err Error, and err contains an error message.
-- @retval  not nil Netbox-like object.
--
local function wrap_text_socket(connection, url)
    local conn = setmetatable({
        _socket = connection,
        state = 'active',
        host = url.host or 'localhost',
        port = url.service,
    }, text_connection_mt)
    if not conn:write('require("console").delimiter("$EOF$")\n') or
       not conn:read() then
        conn:set_error()
    end
    return conn
end

--
-- Evaluate command on remote instance
--
local function remote_eval(self, line)
    if line and self.remote.state == 'active' then
        local ok, res = pcall(self.remote.eval, self.remote, line)
        if self.remote.state == 'active' then
            return ok and res or format(false, res)
        end
    end
    local err = self.remote.error
    self.remote:close()
    self.remote = nil
    self.eval = nil
    self.prompt = nil
    self.completion = nil
    pcall(self.on_client_disconnect, self)
    return (err and format(false, err)) or ''
end

--
-- Read command from stdin
--
local function local_read(self)
    local buf = ""
    local prompt = self.prompt
    while true do
        local delim = self.delimiter
        local line = internal.readline({
            prompt = prompt.. "> ",
            completion = self.ac and self.completion or nil
        })
        if not line then
            return nil
        end
        buf = buf..line
        if delim == "" then
            -- stop once a complete Lua statement is entered
            local fn, err = loadstring(buf)
            if fn ~= nil or not string.find(err, " near '<eof>'$") then
                -- valid Lua code or a syntax error not due to
                -- an incomplete input
                break
            end
            if loadstring('return '..buf) ~= nil then
                -- certain obscure inputs like '(42\n)' yield the
                -- same error as incomplete statement
                break
            end
        elseif #buf >= #delim and buf:sub(#buf - #delim + 1) == delim then
            buf = buf:sub(0, #buf - #delim)
            break
        end
        buf = buf.."\n"
        prompt = string.rep(' ', #self.prompt)
    end
    internal.add_history(buf)
    if self.history_file then
        internal.save_history(self.history_file)
    end
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
    return buf:sub(1, -#delim-1)
end

--
-- Print result to connected client from console.listen()
--
local function client_print(self, output)
    if not self.client then
        return
    elseif not output then
        -- disconnect peer
        self.client = nil -- socket will be closed by tcp_server() function
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
        completion = internal.completion_handler;
        ac = true;
    };
}

--
-- REPL = read-eval-print-loop
--
local function repl(self)
    fiber.self().storage.console = self
    if type(self.on_start) == 'function' then
        self:on_start()
    end

    while self.running do
        local command = self:read()
        local output = self:eval(command)
        self:print(output)
    end
    fiber.self().storage.console = nil
end

local function on_start(foo)
    if foo == nil or type(foo) == 'function' then
        repl_mt.__index.on_start = foo
        return
    end
    error('Wrong type of on_start hook: ' .. type(foo))
end

local function on_client_disconnect(foo)
    if foo == nil or type(foo) == 'function' then
        repl_mt.__index.on_client_disconnect = foo
        return
    end
    error('Wrong type of on_client_disconnect hook: ' .. type(foo))
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
--
--
local function ac(yes_no)
    local self = fiber.self().storage.console
    if self == nil then
        error("console.ac(): need existing console")
    end
    self.ac = not not yes_no
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
    local home_dir = os.getenv('HOME')
    if home_dir then
        self.history_file = home_dir .. '/.tarantool_history'
        internal.load_history(self.history_file)
    end
    session_internal.create(-1, "repl")
    repl(self)
    started = false
end

--
-- Connect to remove instance
--
local function connect(uri, opts)
    opts = opts or {}

    local self = fiber.self().storage.console
    if self == nil then
        error("console.connect() need existing console")
    end

    local u
    if uri then
        u = urilib.parse(tostring(uri))
    end
    if u == nil or u.service == nil then
        error('Usage: console.connect("[login:password@][host:]port")')
    end

    local connection, greeting =
        net_box.establish_connection(u.host, u.service, opts.timeout)
    if not connection then
        log.verbose(greeting)
        box.error(box.error.NO_CONNECTION)
    end
    local remote
    if greeting.protocol == 'Lua console' then
        remote = wrap_text_socket(connection, u)
    else
        opts = {
            connect_timeout = opts.timeout,
            user = u.login,
            password = u.password,
        }
        remote = net_box.wrap(connection, greeting, u.host, u.service, opts)
        if not remote.host then
            remote.host = 'localhost'
        end
        local old_eval = remote.eval
        remote.eval = function(con, line)
            return old_eval(con, 'return require("console").eval(...)', {line})
        end
    end

    -- check connection && permissions
    local ok, res = pcall(remote.eval, remote, 'return true')
    if not ok then
        remote:close()
        pcall(self.on_client_disconnect, self)
        error(res)
    end

    -- override methods
    self.remote = remote
    self.eval = remote_eval
    self.prompt = string.format("%s:%s", self.remote.host, self.remote.port)
    self.completion = function (str, pos1, pos2)
        local c = string.format(
            'return require("console").completion_handler(%q, %d, %d)',
            str, pos1, pos2)
        return yaml.decode(remote:eval(c))[1]
    end
    log.info("connected to %s:%s", self.remote.host, self.remote.port)
    return true
end

local function client_handler(client, peer)
    session_internal.create(client:fd(), "console")
    session_internal.run_on_connect()
    session_internal.run_on_auth(box.session.user(), true)
    local state = setmetatable({
        running = true;
        read = client_read;
        print = client_print;
        client = client;
    }, repl_mt)
    local version = _TARANTOOL:match("([^-]+)-")
    state:print(string.format("%-63s\n%-63s\n",
        "Tarantool ".. version.." (Lua console)",
        "type 'help' for interactive help"))
    repl(state)
    session_internal.run_on_disconnect()
end

--
-- Start admin console
--
local function listen(uri)
    local host, port
    if uri == nil then
        host = 'unix/'
        port = '/tmp/tarantool-console.sock'
    else
        local u = urilib.parse(tostring(uri))
        if u == nil or u.service == nil then
            error('Usage: console.listen("[host:]port")')
        end
        host = u.host
        port = u.service or 3313
    end
    local s, addr = socket.tcp_server(host, port, { handler = client_handler,
        name = 'console'})
    if not s then
        error(string.format('failed to create server %s:%s: %s',
            host, port, errno.strerror()))
    end
    return s
end

package.loaded['console'] = {
    start = start;
    eval = eval;
    delimiter = delimiter;
    ac = ac;
    connect = connect;
    listen = listen;
    on_start = on_start;
    on_client_disconnect = on_client_disconnect;
    completion_handler = internal.completion_handler;
}
