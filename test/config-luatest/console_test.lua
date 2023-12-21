local yaml = require('yaml')
local fio = require('fio')
local socket = require('socket')
local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local replicaset = require('test.config-luatest.replicaset')

local g = t.group()

g.before_all(replicaset.init)
g.after_each(replicaset.drop)
g.after_all(replicaset.clean)

-- Connect to a text console.
local function connect(replicaset, instance_name, config, timeout)
    -- Determine a path.
    --
    -- NB: Here we read only global scope values. No group,
    -- replicaset, instance ones.
    local control_path = fio.pathjoin('var/run', instance_name,
        'tarantool.control')
    if config ~= nil and config.console ~= nil and
       config.console.socket ~= nil then
        control_path = config.console.socket:gsub('{{ instance_name }}',
            instance_name)
    end
    if config ~= nil and config.process ~= nil and
       config.process.work_dir ~= nil then
        control_path = fio.pathjoin(config.process.work_dir, control_path)
    end

    -- Connect.
    local control_path = fio.pathjoin(replicaset._dir, control_path)
    local s = t.helpers.retrying({timeout = timeout or 60}, function()
        local s, err = socket.tcp_connect('unix/', control_path)
        if s == nil then
            error(err)
        end
        return s
    end)

    -- Read the greeting.
    t.assert_str_matches(s:read('\n'), 'Tarantool .+ %(Lua console%).*')
    t.assert_str_matches(s:read('\n'), "type 'help' for interactive help.*")
    return s
end

-- Write the given command, read the answer.
local function eval(s, command)
    s:write(command .. '\n')
    local raw_reply = s:read('...\n')
    local reply = yaml.decode(raw_reply)
    return unpack(reply, 1, table.maxn(reply))
end

local function assert_console_works(replicaset, config)
    local s = connect(replicaset, 'i-001', config)
    t.assert_equals(eval(s, 'return 123'), 123)
end

local function assert_console_closed(_replicaset, _config)
    -- TODO (gh-9535): Enable it after a fix in the console
    -- applier. Now it doesn't stop old console sockets.
    --
    -- t.assert_error(connect, replicaset, 'instance-001', config, 0.1)
end

-- Start, reload, change, return back, disable.
g.test_basic = function(g)
    -- Start an instance and verify that the console works.
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :config()
    local replicaset = replicaset.new(g, config)
    replicaset:start()
    assert_console_works(replicaset, config)

    -- Reload and check.
    replicaset:reload(config)
    assert_console_works(replicaset, config)

    -- Change the socket and verify that the new one appears.
    local new_config = cbuilder.new(config)
        :set_global_option('console.socket', 'foo.control')
        :config()
    replicaset:reload(new_config)
    assert_console_works(replicaset, new_config)

    -- Verify that the old console socket is gone.
    assert_console_closed(replicaset, config)

    -- Remove the new socket path.
    replicaset:reload(config)
    assert_console_works(replicaset, config)
    assert_console_closed(replicaset, new_config)

    -- Disable the console.
    local new_config_2 = cbuilder.new(config)
        :set_global_option('console.enabled', false)
        :config()
    replicaset:reload(new_config_2)
    assert_console_closed(replicaset, config)
    assert_console_closed(replicaset, new_config)
    assert_console_closed(replicaset, new_config_2)
end

-- Start with some custom workinf directory and reload.
g.test_custom_work_dir = function(g)
    local config = cbuilder.new()
        :set_global_option('process.work_dir', 'w')
        :add_instance('i-001', {})
        :config()

    local replicaset = replicaset.new(g, config)
    replicaset:start()
    assert_console_works(replicaset, config)

    replicaset:reload(config)
    assert_console_works(replicaset, config)
end

-- As previous, but with `../` in the socket path.
g.test_parent_dir_in_socket_path = function(g)
    local config = cbuilder.new()
        :set_global_option('process.work_dir', 'w')
        :set_global_option('console.socket', '../foo.control')
        :add_instance('i-001', {})
        :config()

    local replicaset = replicaset.new(g, config)
    replicaset:start()
    assert_console_works(replicaset, config)

    replicaset:reload(config)
    assert_console_works(replicaset, config)
end
