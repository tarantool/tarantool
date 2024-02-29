#!/usr/bin/env tarantool

local CONSOLE_SOCKET_PATH = 'unix/:/var/run/tarantool/tarantool.sock'
local CFG_FILE_PATH = '/etc/tarantool/config.yml'

local yaml = require('yaml')
local console = require('console')
local errno = require('errno')

local function read_config()
    local f = io.open(CFG_FILE_PATH, "rb")
    if f == nil then
        print("Can't open " .. CFG_FILE_PATH ..": ", errno.strerror())
        os.exit(1)
    end
    local content = f:read("*all")
    f:close()
    return yaml.decode(content)
end

local function write_config(cfg)
    local f = io.open(CFG_FILE_PATH, "w+")
    if f == nil then
        print("Can't open " .. CFG_FILE_PATH ..": ", errno.strerror())
        os.exit(1)
    end
    local content = yaml.encode(cfg)
    f:write(content)
    f:close()
end

local function nop(console, cfg, value)
end

local function update_replication_source(console, cfg, value)
    local user_name = "nil"
    if cfg['TARANTOOL_USER_NAME'] then
        user_name = "'" .. cfg['TARANTOOL_USER_NAME'] .. "'"
    end

    local user_password = "nil"
    if cfg['TARANTOOL_USER_PASSWORD'] then
        user_password = "'" .. cfg['TARANTOOL_USER_PASSWORD'] .. "'"
    end

    local cmd = "set_replication_source('"..value.."', " .. user_name .. "," .. user_password .. ")"
    print("cmd: ", cmd)

    local res = console:eval(cmd)

    if res ~= nil then
        print(res)
    end
end

local function update_credentials(console, cfg, value)
    local user_name = "nil"
    if cfg['TARANTOOL_USER_NAME'] then
        user_name = "'" .. cfg['TARANTOOL_USER_NAME'] .. "'"
    end

    local user_password = "nil"
    if cfg['TARANTOOL_USER_PASSWORD'] then
        user_password = "'" .. cfg['TARANTOOL_USER_PASSWORD'] .. "'"
    end

    local cmd = "set_credentials(" .. user_name .. "," .. user_password .. ")"

    local res = console:eval(cmd)

    if res ~= nil then
        print(res)
    end

    local replication_source = cfg['TARANTOOL_REPLICATION_SOURCE']

    if replication_source ~= nil then
        update_replication_source(console, cfg, replication_source)
    end
end


local vars = {
    TARANTOOL_SLAB_ALLOC_ARENA=nop,
    TARANTOOL_SLAB_ALLOC_FACTOR=nop,
    TARANTOOL_SLAB_ALLOC_MAXIMAL=nop,
    TARANTOOL_SLAB_ALLOC_MINIMAL=nop,
    TARANTOOL_PORT=nop,
    TARANTOOL_FORCE_RECOVERY=nop,
    TARANTOOL_LOG_FORMAT=nop,
    TARANTOOL_LOG_LEVEL=nop,
    TARANTOOL_WAL_MODE=nop,
    TARANTOOL_USER_NAME=update_credentials,
    TARANTOOL_USER_PASSWORD=update_credentials,
    TARANTOOL_REPLICATION_SOURCE=update_replication_source,
    TARANTOOL_REPLICATION=update_replication_source,
}

console.on_start(function(self)
    local status, reason
    status, reason = pcall(function() require('console').connect(CONSOLE_SOCKET_PATH) end)
    if not status then
        self:print(reason)
        os.exit(1)
    end

    if arg[1] == nil or arg[2] == nil then
        self:print("Usage: " .. arg[0] .. " <variable> <value>")
        os.exit(1)
    end

    if vars[arg[1]] == nil then
        self:print("Unknown var: " .. arg[1])
        os.exit(1)
    end

    local cfg = read_config()
    cfg[arg[1]] = arg[2]

    local func = vars[arg[1]]
    func(self, cfg, arg[2])

    write_config(cfg)

    self.running = false
    os.exit(0)
end)

console.on_client_disconnect(function(self) self.running = false end)
console.start()

os.exit(0)
