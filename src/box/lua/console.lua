-- console.lua -- internal file
--
-- vim: ts=4 sw=4 et

local ffi = require('ffi')
ffi.cdef[[
    enum output_format {
        OUTPUT_FORMAT_YAML = 0,
        OUTPUT_FORMAT_LUA_LINE,
        OUTPUT_FORMAT_LUA_BLOCK,
    };

    enum output_format
    console_get_output_format();

    void
    console_set_output_format(enum output_format output_format);
]]

local internal = require('console')
local session_internal = require('box.internal.session')
local fiber = require('fiber')
local socket = require('socket')
local log = require('log')
local errno = require('errno')
local urilib = require('uri')
local yaml = require('yaml')
local net_box = require('net.box')
local box_internal = require('box.internal')
local help = require('help').help

local PUSH_TAG_HANDLE = '!push!'

--
-- Default output handler set to YAML for backward
-- compatibility reason.
local default_output_format = { ["fmt"] = "yaml", ["opts"] = nil }
local output_handlers = { }

--
-- This is an end-of-stream marker for text output of Tarantool
-- server, for different output formats - yaml and Lua.
--
-- While for yaml ... is a standard stream end marker, using ';'
-- for Lua output we're walking on a thin ice - ';' is legal inside
-- a valid Lua block as well. But since this marker is used when
-- reading data created by a Tarantool server, we can rely on it not
-- being present inside of stream, since Tarantool server never
-- puts it inside of a stream.
local output_eos = { ["yaml"] = '\n...\n', ["lua"] = ';' }

output_handlers["yaml"] = function(status, opts, ...)
    local err
    if status then
        -- serializer can raise an exception
        status, err = pcall(internal.format_yaml, ...)
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
    return internal.format_yaml({ error = err })
end

--
-- Format a Lua value.
local function format_lua_value(status, internal_opts, value)
    local err
    if status then
        status, err = pcall(internal.format_lua, internal_opts, value)
        if status then
            return err
        else
            local m = 'console: exception while formatting output: "%s"'
            err = m:format(tostring(err))
        end
    else
        err = value
        if err == nil then
            err = box.NULL
        end
    end
    return internal.format_lua(internal_opts, { error = err })
end

--
-- Convert options from user form to the internal format.
local function gen_lua_opts(opts)
    if opts == "block" then
        return {block=true, indent=2}
    else
        return {block=false, indent=2}
    end
end

output_handlers["lua"] = function(status, opts, ...)
    local collect = {}
    --
    -- Convert options to internal dictionary.
    local internal_opts = gen_lua_opts(opts)
    for i = 1, select('#', ...) do
        collect[i] = format_lua_value(status, internal_opts, select(i, ...))
        if collect[i] == nil then
            --
            -- table.concat doesn't work for nil
            -- so convert it explicitly.
            collect[i] = "nil"
        end
    end
    return table.concat(collect, ', ') .. output_eos["lua"]
end

local function output_verify_opts(fmt, opts)
    if opts == nil then
        return nil
    end
    if fmt == "lua" then
        if opts ~= "line" and opts ~= "block" then
            local msg = 'Wrong option "%s", expecting: line or block.'
            return msg:format(opts)
        end
    end
    return nil
end

local function parse_output(value)
    local fmt, opts
    if not value then
        return 'Specify output format: lua or yaml.'
    end
    if value:match("([^,]+),([^,]+)") ~= nil then
        fmt, opts = value:match("([^,]+),([^,]+)")
    else
        fmt = value
    end
    for k, _ in pairs(output_handlers) do
        if k == fmt then
            local err = output_verify_opts(fmt, opts)
            if err then
                return err
            end
            return nil, fmt, opts
        end
    end
    local msg = 'Invalid format "%s", supported languages: lua and yaml.'
    return msg:format(value)
end

local function set_default_output(value)
    if value == nil then
        error("Nil output value passed")
    end
    local err, fmt, opts = parse_output(value)
    if err then
        error(err)
    end
    default_output_format["fmt"] = fmt
    default_output_format["opts"] = opts
end

local function get_default_output(...)
    local args = ...
    if args ~= nil then
        error("Arguments provided while prohibited")
    end
    return default_output_format
end

local function output_save(fmt, opts)
    --
    -- Output format descriptors are saved per
    -- session thus each console may specify
    -- own mode.
    if fmt == "yaml" then
        ffi.C.console_set_output_format(ffi.C.OUTPUT_FORMAT_YAML)
    elseif fmt == "lua" and opts == "block" then
        ffi.C.console_set_output_format(ffi.C.OUTPUT_FORMAT_LUA_BLOCK)
    else
        ffi.C.console_set_output_format(ffi.C.OUTPUT_FORMAT_LUA_LINE)
    end
end

local function current_output()
    local fmt = ffi.C.console_get_output_format()
    if fmt == ffi.C.OUTPUT_FORMAT_YAML then
        return { ["fmt"] = "yaml", ["opts"] = nil }
    elseif fmt == ffi.C.OUTPUT_FORMAT_LUA_LINE then
        return { ["fmt"] = "lua", ["opts"] = "line" }
    elseif fmt == ffi.C.OUTPUT_FORMAT_LUA_BLOCK then
        return { ["fmt"] = "lua", ["opts"] = "block" }
    end
end

-- Used by console_session_push.
box_internal.format_lua_push = function(value)
    local internal_opts = gen_lua_opts(current_output()["opts"])
    value = format_lua_value(true, internal_opts, value)
    return '-- Push\n' .. value .. ';'
end

--
-- Return current EOS value for currently
-- active output format.
local function current_eos()
    return output_eos[current_output()["fmt"]]
end

--
-- Set/get current console EOS value from
-- currently active output format.
local function console_eos(eos_value)
    if not eos_value then
        return tostring(current_eos())
    end
    -- We can't allow to change yaml eos format
    -- because it is a part of encoding standart.
    local d = current_output()
    if d["fmt"] == "yaml" then
        error("console.eos(): is immutable for output " .. d["fmt"])
    else
        output_eos[d["fmt"]] = eos_value
    end
end

--
-- Map output format descriptor into a "\set" command.
local function output_to_cmd_string(desc)
    if desc["opts"] then
        return string.format("\\set output %s,%s", desc["fmt"], desc["opts"])
    else
        return string.format("\\set output %s", desc["fmt"])
    end
end

local function format(status, ...)
    local d = current_output()
    return output_handlers[d["fmt"]](status, d["opts"], ...)
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

local function set_delimiter(storage, value)
    local console = fiber.self().storage.console
    if console ~= nil and console.delimiter == '$EOF$' then
        return error('Can not install delimiter for net box sessions')
    end
    value = value or ''
    return delimiter(value)
end

local function set_language(storage, value)
    if value == nil then
        return { language = storage.language or 'lua' }
    end
    if value ~= 'lua' and value ~= 'sql' then
        local msg = 'Invalid language "%s", supported languages: lua and sql.'
        return error(msg:format(value))
    end
    storage.language = value
    return true
end

local function set_output(storage, value)
    local err, fmt, opts = parse_output(value)
    if err then
        return error(err)
    end
    output_save(fmt, opts)
    return true
end

local param_handlers = {
    language = set_language,
    lang = set_language,
    l = set_language,
    output = set_output,
    o = set_output,
    delimiter = set_delimiter,
    delim = set_delimiter,
    d = set_delimiter
}

local function set_param(storage, func, param, value)
    if param == nil then
        return format(false, 'Invalid set syntax, type \\help for help')
    end
    if param_handlers[param] == nil then
        return format(false, 'Unknown parameter: ' .. tostring(param))
    end
    return format(pcall(param_handlers[param], storage, value))
end

local function help_wrapper(storage)
    return format(true, help())
end

local function quit(storage)
    local console = fiber.self().storage.console
    if console ~= nil then
        console.running = false
    end
end

local operators = {
    help = help_wrapper,
    h = help_wrapper,
    set = set_param,
    s = set_param,
    quit = quit,
    q = quit
}

--
-- Generate command arguments from the line to
-- be passed into command handlers.
local function parse_operators(line)
    local items = {}
    for item in string.gmatch(line, '([^%s]+)') do
        items[#items + 1] = item
    end
    if #items == 0 then
        return 0, nil
    end
    if operators[items[1]] == nil then
        return #items, nil
    end
    return #items, items
end

--
-- Preprocess a command via operator helpers.
local function preprocess(storage, line)
    local nr_items, items = parse_operators(line)
    if nr_items == 0 then
        return help_wrapper()
    end
    if items == nil then
        local msg = "Invalid command \\%s. Type \\help for help."
        return format(false, msg:format(line))
    end
    return operators[items[1]](storage, unpack(items))
end

--
-- Return a command without a leading slash.
local function get_command(line)
    if line:sub(1, 1) == '\\' then
        return line:sub(2)
    end
    return nil
end

--
-- Evaluate command on local instance
--
local function local_eval(storage, line)
    if not line then
        return nil
    end
    local command = get_command(line)
    if command then
        return preprocess(storage, command)
    end
    if storage.language == 'sql' then
        return format(pcall(box.execute, line))
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
    return local_eval(box.session, line)
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
        -- Read a text from a socket until EOS.
        -- @retval not nil Well formatted data.
        -- @retval     nil Error.
        --
        read = function(self)
            local ret = self._socket:read(self.eos)
            if ret and ret ~= '' then
                return ret
            end
        end,
        --
        -- The command might modify EOS so
        -- we have to parse and preprocess it
        -- first to not stuck in :read() forever.
        --
        -- Same time the EOS is per connection
        -- value so we keep it inside the metatable
        -- instace, and because we can't use same
        -- name as output_eos() function we call
        -- it simplier as self.eos.
        preprocess_eval = function(self, text)
            local command = get_command(text)
            if command == nil then
                return
            end
            local nr_items, items = parse_operators(command)
            if nr_items == 3 then
                --
                -- Make sure it is exactly "\set output" command.
                if operators[items[1]] == set_param and
                    param_handlers[items[2]] == set_output then
                    local err, fmt = parse_output(items[3])
                    if not err then
                        self.fmt = fmt
                        self.eos = output_eos[fmt]
                    end
                end
            end
        end,
        --
        -- Write + Read.
        --
        eval = function(self, text)
            self:preprocess_eval(text)
            text = text..'$EOF$\n'
            local fmt = self.fmt or default_output_format["fmt"]
            if not self:write(text) then
                error(self:set_error())
            end
            while true do
                local rc = self:read()
                if not rc then
                    break
                end
                if fmt == "yaml" then
                    local handle = yaml.decode(rc, {tag_only = true})
                    if not handle then
                        -- Can not fail - tags are encoded with no
                        -- user participation and are correct always.
                        return rc
                    end
                    if handle == PUSH_TAG_HANDLE and self.print_f then
                        self.print_f(rc)
                    end
                else
                    if not string.startswith(rc, '-- Push') then
                        return rc
                    end
                    if self.print_f then
                        self.print_f(rc)
                    end
                end
            end
            return error(self:set_error())
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
-- @param print_f Function to print push messages.
--
-- @retval nil, err Error, and err contains an error message.
-- @retval  not nil Netbox-like object.
--
local function wrap_text_socket(connection, url, print_f)
    local conn = setmetatable({
        _socket = connection,
        state = 'active',
        host = url.host or 'localhost',
        port = url.service,
        print_f = print_f,
        eos = current_eos(),
        fmt = current_output()["fmt"]
    }, text_connection_mt)
    --
    -- Prepare the connection: setup EOS symbol
    -- by executing the \set command on remote machine
    -- explicitly, and then setup a delimiter.
    local cmd_set_output = output_to_cmd_string(current_output()) .. '\n'
    if not conn:write(cmd_set_output) or not conn:read() then
        conn:set_error()
    end
    local cmd_delimiter = 'require("console").delimiter("$EOF$")\n'
    if not conn:write(cmd_delimiter) or not conn:read() then
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

local function local_check_lua(buf)
    local fn, err = loadstring(buf)
    if fn ~= nil or not string.find(err, " near '<eof>'$") then
        -- valid Lua code or a syntax error not due to
        -- an incomplete input
        return true
    end
    if loadstring('return '..buf) ~= nil then
        -- certain obscure inputs like '(42\n)' yield the
        -- same error as incomplete statement
        return true
    end
    return false
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
        if buf:sub(1, 1) == '\\' then
            break
        end
        if delim == "" then
            local lang = box.session.language
            if not lang or lang == 'lua' then
                -- stop once a complete Lua statement is entered
                if local_check_lua(buf) then
                    break
                end
            else
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
    --
    -- Byte sequences that come over the network and come from
    -- the local client console may have a different terminal
    -- character. We support both possible options: LF and CRLF.
    --
    local delim_cr = self.delimiter .. "\r"
    local delim_lf = self.delimiter .. "\n"
    local buf = self.client:read({delimiter = {delim_lf, delim_cr}})
    if buf == nil then
        return nil
    elseif buf == "" then
        return nil -- EOF
    elseif buf == "~.\n" then
        -- Escape sequence to close current connection (like SSH)
        return nil
    end
    -- remove trailing delimiter
    return buf:sub(1, -#self.delimiter-2)
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
    session_internal.create(1, "repl") -- stdin fileno
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
        remote = wrap_text_socket(connection, u,
                                  function(msg) self:print(msg) end)
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
    local s = socket.tcp_server(host, port, { handler = client_handler,
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
    set_default_output = set_default_output;
    get_default_output = get_default_output;
    eos = console_eos;
    ac = ac;
    connect = connect;
    listen = listen;
    on_start = on_start;
    on_client_disconnect = on_client_disconnect;
    completion_handler = internal.completion_handler;
}
