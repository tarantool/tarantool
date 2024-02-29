#!/usr/bin/env tarantool

local fio = require('fio')
local errno = require('errno')
local urilib = require('uri')
local console = require('console')
local term = require('term')
local log = require('log')
local yaml = require('yaml')

local TARANTOOL_DEFAULT_PORT = 3301
local CONSOLE_SOCKET_PATH = 'unix/:/var/run/tarantool/tarantool.sock'
local CFG_FILE_PATH = '/etc/tarantool/config.yml'


local orig_cfg = box.cfg

local function read_config()
    local f = io.open(CFG_FILE_PATH, "rb")
    if f == nil then
        log.error("Can't open " .. CFG_FILE_PATH ..": ", errno.strerror())
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

local function parse_replication_source(replication_source, user_name, user_password)
    if replication_source == nil then
        return nil
    end

    local replication_source_table = {}
    for uri in string.gmatch(replication_source, "[^,]+") do
        local parsed_uri = urilib.parse(uri)
        if parsed_uri == nil then
            error("Incorrect replication source URI format: '"..uri.."'")
        end
        local host = parsed_uri.host
        local port = parsed_uri.service or TARANTOOL_DEFAULT_PORT
        local user = parsed_uri.login or user_name
        local password = parsed_uri.password or user_password

        if user == 'guest' or user == nil then
            replication_source = string.format("%s:%s", host, port)
        elseif password == nil then
            replication_source = string.format("%s:@%s:%s", user, host, port)
        else
            replication_source = string.format("%s:%s@%s:%s", user, password,
                                               host, port)
        end

        table.insert(replication_source_table, replication_source)
    end

    return replication_source_table
end

local function choose_option(main, substitute, cfg)
    if cfg[main] then
        return main
    end
    if cfg[substitute] then
        return substitute
    end
    return main
end

function set_replication_source(replication_source, user_name, user_password)
    local replication_source_table = parse_replication_source(
        replication_source, user_name, user_password
    )
    local choice = choose_option('replication', 'replication_source', box.cfg)
    box.cfg{[choice] = replication_source_table}
    log.info("Updated box.cfg.%s to %s", choice, replication_source)
end

local function create_user(user_name, user_password)
    if user_name ~= 'guest' and user_password == nil then
        user_password = ""

        local warn_str = [[****************************************************
WARNING: No password has been set for the database.
         This will allow anyone with access to the
         Tarantool port to access your database. In
         Docker's default configuration, this is
         effectively any other container on the same
         system.
         Use "-e TARANTOOL_USER_PASSWORD=password"
         to set it in "docker run".
****************************************************]]
        log.warn('\n'..warn_str)
    end

    if user_name == 'guest' and user_password == nil then
        local warn_str = [[****************************************************
WARNING: 'guest' is chosen as primary user.
         Since it is not allowed to set a password for
         guest user, your instance will be accessible
         by anyone having direct access to the Tarantool
         port.
         If you wanted to create an authenticated user,
         specify "-e TARANTOOL_USER_NAME=username" and
         pick a user name other than "guest".
****************************************************]]
        log.warn('\n'..warn_str)
    end

    if user_name == 'guest' and user_password ~= nil then
        user_password = nil

        local warn_str = [[****************************************************
WARNING: A password for guest user has been specified.
         In Tarantool, guest user can't have a password
         and is always allowed to login, if it has
         enough privileges.
         If you wanted to create an authenticated user,
         specify "-e TARANTOOL_USER_NAME=username" and
         pick a user name other than "guest".
****************************************************]]
        log.warn('\n'..warn_str)
    end

    if user_name ~= 'admin' and user_name ~= 'guest' then
        if not box.schema.user.exists(user_name) then
            log.info("Creating user '%s'", user_name)
            box.schema.user.create(user_name)
        end
    end

    if user_name ~= 'admin' then
        log.info("Granting admin privileges to user '%s'", user_name)
        box.schema.user.grant(user_name, 'read,write,execute,create,drop',
                              'universe', nil, {if_not_exists = true})
        box.schema.user.grant(user_name, 'replication',
                              nil, nil, {if_not_exists = true})
    end

    if user_name ~= 'guest' then
        log.info("Setting password for user '%s'", user_name)
        box.schema.user.passwd(user_name, user_password)
    end
end

function set_credentials(user_name, user_password)
    create_user(user_name, user_password)
end

local function wrapper_cfg(override)
    local work_dir = '/var/lib/tarantool'
    local snap_filename = "*.snap"
    local snap_path = work_dir..'/'..snap_filename

    local first_run = false
    if next(fio.glob(snap_path)) == nil then
        first_run = true
    end


    local file_cfg = {}
    local config_file_exists = fio.stat(CFG_FILE_PATH) ~= nil
    if not config_file_exists then
        log.info("Creating configuration file: " .. CFG_FILE_PATH)

        file_cfg.TARANTOOL_USER_NAME = os.getenv('TARANTOOL_USER_NAME')
        file_cfg.TARANTOOL_USER_PASSWORD = os.getenv('TARANTOOL_USER_PASSWORD')
        file_cfg.TARANTOOL_SLAB_ALLOC_ARENA = os.getenv('TARANTOOL_SLAB_ALLOC_ARENA')
        file_cfg.TARANTOOL_SLAB_ALLOC_FACTOR = os.getenv('TARANTOOL_SLAB_ALLOC_FACTOR')
        file_cfg.TARANTOOL_SLAB_ALLOC_MINIMAL = os.getenv('TARANTOOL_SLAB_ALLOC_MINIMAL')
        file_cfg.TARANTOOL_SLAB_ALLOC_MAXIMAL = os.getenv('TARANTOOL_SLAB_ALLOC_MAXIMAL')
        file_cfg.TARANTOOL_PORT = os.getenv('TARANTOOL_PORT')
        file_cfg.TARANTOOL_FORCE_RECOVERY = os.getenv('TARANTOOL_FORCE_RECOVERY')
        file_cfg.TARANTOOL_LOG_FORMAT = os.getenv('TARANTOOL_LOG_FORMAT')
        file_cfg.TARANTOOL_LOG_LEVEL = os.getenv('TARANTOOL_LOG_LEVEL')
        file_cfg.TARANTOOL_WAL_MODE = os.getenv('TARANTOOL_WAL_MODE')
        file_cfg.TARANTOOL_REPLICATION_SOURCE = os.getenv('TARANTOOL_REPLICATION_SOURCE')
        file_cfg.TARANTOOL_REPLICATION = os.getenv('TARANTOOL_REPLICATION')
        file_cfg.TARANTOOL_SNAPSHOT_PERIOD = os.getenv('TARANTOOL_SNAPSHOT_PERIOD')
        file_cfg.TARANTOOL_MEMTX_MEMORY = os.getenv('TARANTOOL_MEMTX_MEMORY')
        file_cfg.TARANTOOL_CHECKPOINT_INTERVAL = os.getenv('TARANTOOL_CHECKPOINT_INTERVAL')
        file_cfg.TARANTOOL_MEMTX_MIN_TUPLE_SIZE = os.getenv('TARANTOOL_MEMTX_MIN_TUPLE_SIZE')
        file_cfg.TARANTOOL_MEMTX_MAX_TUPLE_SIZE = os.getenv('TARANTOOL_MEMTX_MAX_TUPLE_SIZE')

        write_config(file_cfg)
    else
        log.info("Loading existing configuration file: " .. CFG_FILE_PATH)

        file_cfg = read_config()
    end

    local user_name = file_cfg.TARANTOOL_USER_NAME or
        os.getenv('TARANTOOL_USER_NAME') or 'guest'
    local user_password = file_cfg.TARANTOOL_USER_PASSWORD or
        os.getenv('TARANTOOL_USER_PASSWORD')


    local cfg = override or {}
    -- Placeholders for deprecated options
    cfg.slab_alloc_arena = tonumber(file_cfg.TARANTOOL_SLAB_ALLOC_ARENA) or
        override.slab_alloc_arena
    cfg.slab_alloc_maximal = tonumber(file_cfg.TARANTOOL_SLAB_ALLOC_MAXIMAL) or
        override.slab_alloc_maximal
    cfg.slab_alloc_minimal = tonumber(file_cfg.TARANTOOL_SLAB_ALLOC_MINIMAL) or
        override.slab_alloc_minimal
    cfg.snapshot_period = tonumber(file_cfg.TARANTOOL_SNAPSHOT_PERIOD) or
        override.snapshot_period
    -- Replacements for deprecated options
    cfg.memtx_memory = tonumber(file_cfg.TARANTOOL_MEMTX_MEMORY) or
        override.memtx_memory
    cfg.memtx_min_tuple_size = tonumber(file_cfg.TARANTOOL_MEMTX_MIN_TUPLE_SIZE) or
        override.memtx_min_tuple_size
    cfg.memtx_max_tuple_size = tonumber(file_cfg.TARANTOOL_MEMTX_MAX_TUPLE_SIZE) or
        override.memtx_max_tuple_size
    cfg.checkpoint_interval = tonumber(file_cfg.TARANTOOL_CHECKPOINT_INTERVAL) or
        override.checkpoint_interval
    -- Deprecated options with default values
    local choice = choose_option('memtx_dir', 'snap_dir', override)
    cfg[choice] = override[choice] or '/var/lib/tarantool'

    -- Remaining configuration
    cfg.slab_alloc_factor = tonumber(file_cfg.TARANTOOL_SLAB_ALLOC_FACTOR) or
        override.slab_alloc_factor
    cfg.listen = tonumber(file_cfg.TARANTOOL_PORT) or
        override.listen or TARANTOOL_DEFAULT_PORT
    cfg.wal_mode = file_cfg.TARANTOOL_WAL_MODE or
        override.wal_mode

    cfg.force_recovery = file_cfg.TARANTOOL_FORCE_RECOVERY == 'true'
    cfg.log_format = file_cfg.TARANTOOL_LOG_FORMAT or 'plain'
    cfg.log_level = tonumber(file_cfg.TARANTOOL_LOG_LEVEL) or 5

    cfg.wal_dir = override.wal_dir or '/var/lib/tarantool'
    cfg.vinyl_dir = override.vinyl_dir or '/var/lib/tarantool'
    cfg.pid_file = override.pid_file or '/var/run/tarantool/tarantool.pid'

    local choice = choose_option('TARANTOOL_REPLICATION', 'TARANTOOL_REPLICATION_SOURCE', file_cfg)
    local replication_source_table = parse_replication_source(file_cfg[choice],
                                                              user_name,
                                                              user_password)

    if replication_source_table then
        cfg.replication = replication_source_table
    else
        local choice = choose_option('replication', 'replication_source', override)
        cfg[choice] = override[choice]
    end

    log.info("Config:\n" .. yaml.encode(cfg))

    orig_cfg(cfg)

    box.once('tarantool-entrypoint', function ()
        if first_run then
            log.info("Initializing database")

            create_user(user_name, user_password)
        end
    end)

    console.listen(CONSOLE_SOCKET_PATH)

    local metrics_port = tonumber(os.getenv('TARANTOOL_PROMETHEUS_DEFAULT_METRICS_PORT')) or 0
    if metrics_port > 0 then
        local ok, http_server = pcall(require, 'http.server')
        if not ok then
            local warn_str = [[****************************************************
WARNING: The "http" module is not found!
         Exposing the Prometheus metrics endpoint
         is impossible without HTTP server.
         Please install the module.
****************************************************]]
            log.warn('\n' .. warn_str)
        else
            require('metrics').enable_default_metrics()
            local prometheus = require('metrics.plugins.prometheus')
            local httpd = http_server.new('0.0.0.0', metrics_port)
            httpd:route({path = '/metrics'}, prometheus.collect_http)
            httpd:start()
        end
    end
end

box.cfg = wrapper_cfg

-- re-run the script passed as parameter with all arguments that follow
execute_script = arg[1]
if execute_script == nil then
    box.cfg {}

    if term.isatty(io.stdout) then
        console.start()
        os.exit(0)
    end
else
    narg = 0
    while true do
        arg[narg] = arg[narg + 1]
        if arg[narg] == nil then
            break
        end
        narg = narg + 1
    end

    dofile(execute_script)
end
