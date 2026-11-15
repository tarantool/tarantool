local yaml = require('yaml')
local fio = require('fio')
local socket = require('socket')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()
local g_missing_parent = t.group('missing_console_parent', {
    {reload = false},
    {reload = true},
})

-- Connect to a text console.
local function connect(cluster, instance_name, config, timeout)
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
    local control_path = fio.pathjoin(cluster._dir, control_path)
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

local function assert_console_works(cluster, config, timeout)
    local s = connect(cluster, 'i-001', config, timeout)
    t.assert_equals(eval(s, 'return 123'), 123)
end

local function assert_console_closed(cluster, config)
    t.assert_error(connect, cluster, 'i-001', config, 0.1)
end

-- Start, reload, change, return back, disable.
g.test_basic = function()
    -- Start an instance and verify that the console works.
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    assert_console_works(cluster, config)

    -- Reload and check.
    cluster:reload(config)
    assert_console_works(cluster, config)

    -- Change the socket and verify that the new one appears.
    local new_config = cbuilder:new(config)
        :set_global_option('console.socket', 'foo.control')
        :config()
    cluster:reload(new_config)
    assert_console_works(cluster, new_config)

    -- Verify that the old console socket is gone.
    assert_console_closed(cluster, config)

    -- Remove the new socket path.
    cluster:reload(config)
    assert_console_works(cluster, config)
    assert_console_closed(cluster, new_config)

    -- Disable the console.
    local new_config_2 = cbuilder:new(config)
        :set_global_option('console.enabled', false)
        :config()
    cluster:reload(new_config_2)
    assert_console_closed(cluster, config)
    assert_console_closed(cluster, new_config)
    assert_console_closed(cluster, new_config_2)
end

-- Start with some custom workinf directory and reload.
g.test_custom_work_dir = function()
    local config = cbuilder:new()
        :set_global_option('process.work_dir', 'w')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()
    assert_console_works(cluster, config)

    cluster:reload(config)
    assert_console_works(cluster, config)
end

-- As previous, but with `../` in the socket path.
g.test_parent_dir_in_socket_path = function()
    local config = cbuilder:new()
        :set_global_option('process.work_dir', 'w')
        :set_global_option('console.socket', '../foo.control')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()
    assert_console_works(cluster, config)

    cluster:reload(config)
    assert_console_works(cluster, config)
end

-- Verify console reload compares physical locations, not path spellings.
g.test_symlink_parent_socket_reload = function()
    local config = cbuilder:new()
        :set_global_option('console.socket', 'link/../c.sock')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster:new(config)
    t.assert(fio.mktree(cluster._dir .. '/target/child'))
    t.assert(fio.symlink('target/child', cluster._dir .. '/link'))
    cluster:start()
    assert_console_works(cluster, config)

    local inode = fio.stat(cluster._dir .. '/target/c.sock').inode
    local alias_config = cbuilder:new(config)
        :set_global_option('console.socket', 'target/c.sock')
        :config()
    cluster:reload(alias_config)
    assert_console_works(cluster, alias_config, 2)
    t.assert_equals(fio.stat(cluster._dir .. '/target/c.sock').inode, inode)

    local new_config = cbuilder:new(config)
        :set_global_option('console.socket', 'c.sock')
        :config()
    cluster:reload(new_config)
    assert_console_works(cluster, new_config, 2)
    assert_console_closed(cluster, config)

    cluster:reload(config)
    assert_console_works(cluster, config, 2)
    assert_console_closed(cluster, new_config)
end

-- Verify missing console parents follow symlink/.. at startup and reload.
g_missing_parent.test_symlink_parent_creation = function(cg)
    local config = cbuilder:new()
        :set_global_option('console.socket', 'link/../new/nested/c.sock')
        :add_instance('i-001', {})
        :config()
    local initial = cg.params.reload and cbuilder:new(config)
        :set_global_option('console.socket', 'initial.sock'):config() or config
    local cluster = cluster:new(initial)
    t.assert(fio.mktree(cluster._dir .. '/target/child'))
    t.assert(fio.symlink('target/child', cluster._dir .. '/link'))
    cluster:start()
    if cg.params.reload then
        cluster:reload(config)
    end
    assert_console_works(cluster, config, 2)
    t.assert(fio.path.is_dir(cluster._dir .. '/target/new/nested'))
    t.assert_not(fio.path.exists(cluster._dir .. '/new'))

    local inode = fio.stat(cluster._dir .. '/target/new/nested/c.sock').inode
    local alias = cbuilder:new(config)
        :set_global_option('console.socket', 'target/new/nested/c.sock')
        :config()
    cluster:reload(alias)
    assert_console_works(cluster, alias, 2)
    t.assert_equals(fio.stat(cluster._dir .. '/target/new/nested/c.sock').inode,
                    inode)
end

-- Verify missing console parents keep short binds under a long workdir.
g.test_missing_parent_under_long_work_dir = function()
    local dir = treegen.prepare_directory({}, {})
    local work_dir = dir .. '/' .. string.rep('w', 120)
    t.assert(fio.mktree(work_dir .. '/target/child'))
    t.assert(fio.symlink('target/child', work_dir .. '/link'))
    local script = treegen.write_file(dir, 'main.lua', [[
        if rawget(_G, 'console_check_started') then return end
        rawset(_G, 'console_check_started', true)
        local socket = require('socket')
        local yaml = require('yaml')
        local config = require('config')
        local function verify()
            local s = assert(socket.tcp_connect('unix/',
                                                'target/new/c.sock', 2))
            assert(s:read('\n', 2):find('Lua console', 1, true))
            assert(s:read('\n', 2):find('interactive help', 1, true))
            assert(s:write('return 123\n', 2))
            assert(yaml.decode(s:read('...\n', 2))[1] == 123)
            s:close()
        end
        verify()
        require('fiber').new(function()
            config:wait_status('ready')
            config:reload()
            verify()
            print('long workdir console works')
            os.exit(0)
        end)
    ]])
    local config = cbuilder:new()
        :set_global_option('process.work_dir', work_dir)
        :set_global_option('console.socket', 'link/../new/c.sock')
        :add_instance('i-001', {})
        :config()
    config.iproto = nil
    config.app = {file = script}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {},
        {'--name', 'i-001', '--config', 'config.yaml'},
        {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
    t.assert_str_contains(res.stdout, 'long workdir console works')
    t.assert(fio.path.is_dir(work_dir .. '/target/new'))
    t.assert_not(fio.path.exists(work_dir .. '/new'))
end

-- Verify retargeting a socket parent moves the console and cleans up.
g.test_retargeted_socket_parent = function()
    local config = cbuilder:new()
        :set_global_option('console.socket', 'link/c.sock')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster:new(config)
    t.assert(fio.mktree(cluster._dir .. '/first'))
    t.assert(fio.mktree(cluster._dir .. '/second'))
    t.assert(fio.symlink('first', cluster._dir .. '/link'))
    cluster:start()
    assert_console_works(cluster, config)

    local old_location = cbuilder:new(config)
        :set_global_option('console.socket', 'first/c.sock')
        :config()
    t.assert(fio.unlink(cluster._dir .. '/link'))
    t.assert(fio.symlink('second', cluster._dir .. '/link'))
    cluster:reload(config)
    assert_console_works(cluster, config, 2)
    assert_console_closed(cluster, old_location)
    t.assert_equals(fio.lstat(cluster._dir .. '/first/c.sock'), nil)
end

-- Verify retargeting a socket parent preserves another live listener.
g.test_retargeted_socket_parent_occupied = function()
    local config = cbuilder:new()
        :set_global_option('console.socket', 'link/c.sock')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster:new(config)
    t.assert(fio.mktree(cluster._dir .. '/first'))
    t.assert(fio.mktree(cluster._dir .. '/second'))
    t.assert(fio.symlink('first', cluster._dir .. '/link'))
    cluster:start()
    assert_console_works(cluster, config)

    cluster['i-001']:exec(function()
        local config = require('config')
        local fio = require('fio')
        local socket = require('socket')
        local victim = socket.tcp_server('unix/', 'second/c.sock', function(s)
            s:write('VICTIM\n')
        end)
        t.assert_not_equals(victim, nil)
        local inode = fio.stat('second/c.sock').inode
        t.assert(fio.unlink('link'))
        t.assert(fio.symlink('second', 'link'))
        t.assert_error_msg_contains('Address already in use',
                                   config.reload, config)
        t.assert_equals(fio.lstat('first/c.sock'), nil)
        t.assert_equals(fio.stat('second/c.sock').inode, inode)
        local s = socket.tcp_connect('unix/', 'second/c.sock', 2)
        t.assert_not_equals(s, nil)
        t.assert_equals(s:read('\n', 2), 'VICTIM\n')
        s:close()
        victim:close()
        config:reload()
    end)
    assert_console_works(cluster, config, 2)
end

-- Re-open console by setting enable to false and true
g.test_reopen_console = function()
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    assert_console_works(cluster, config)

    local new_config = cbuilder:new(config)
        :set_global_option('console.enabled', false)
        :config()
    cluster:reload(new_config)
    assert_console_closed(cluster, config)
    assert_console_closed(cluster, new_config)

    local new_config_2 = cbuilder:new(new_config)
        :set_global_option('console.enabled', true)
        :config()
    cluster:reload(new_config_2)
    assert_console_works(cluster, config)
    assert_console_works(cluster, new_config)
    assert_console_works(cluster, new_config_2)
end
