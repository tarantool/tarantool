local t = require('luatest')
local server = require('luatest.server')
local fio = require('fio')

local g = t.group()

g.before_each(function(cg)
    cg.master = server:new()
    cg.master:start()
end)

g.after_each(function(cg)
    cg.master:drop()
    if cg.replica ~= nil then
        cg.replica:drop()
        cg.replica = nil
    end
end)

local test_no_crash_on_shutdown = function(server)
    server.process:kill()
    local path = fio.pathjoin(server.workdir, 'tt_exit_file.txt')
    t.helpers.retrying({}, function()
        t.assert(fio.path.exists(path))
    end)
    local fh, err = fio.open(path, 'O_RDONLY')
    assert(fh, err)
    local str, err = fh:read()
    assert(str, err)
    fh:close()
    t.assert_str_contains(str, 'ExitCode: 0\n')
end

g.test_shutdown_on_rebootstrap = function(cg)
    t.tarantool.skip_if_not_debug()
    -- It is critical for test that we can connect to uri but cannot auth.
    -- In this case cancelling applier will does not cause bootstrap
    -- failure and panic.
    local cfg = {
        replication = 'no:way@' .. cg.master.net_box_uri,
        replication_timeout = 100,
    }
    local env = {
        -- There will be no connection to replica in test.
        TARANTOOL_RUN_BEFORE_BOX_CFG = [[
            box.error.injection.set('ERRINJ_MAIN_MAKE_FILE_ON_RETURN', true)
        ]],
    }
    cg.replica = server:new({box_cfg = cfg, env = env})
    -- Can't not wait because replica will not be bootstrapped.
    cg.replica:start({wait_until_ready = false})
    local retry_msg = string.format('will retry every %.2f second',
                                    cfg.replication_timeout)
    -- No connection over netbox so we need to provide path to log ourselves.
    local log = fio.pathjoin(cg.replica.workdir, cg.replica.alias..'.log')
    t.helpers.retrying({}, function()
        t.assert(cg.replica:grep_log(retry_msg, nil, {filename = log}))
    end)
    test_no_crash_on_shutdown(cg.replica)
end
