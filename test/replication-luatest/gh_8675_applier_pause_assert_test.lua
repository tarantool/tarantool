local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('gh-8675-applier-pause')

local fio = require('fio')

local set_log_before_cfg = [[
    local logfile = require('fio').pathjoin(
        os.getenv('TARANTOOL_WORKDIR'),
        os.getenv('TARANTOOL_ALIAS') .. '.log'
    )
    require('log').cfg{log = logfile}
]]

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.server = cg.replica_set:build_and_add_server{
        alias = 'server',
        box_cfg = {
            replication_connect_timeout = 0.1,
            replication_timeout = 0.01,
        },
        env = {['TARANTOOL_RUN_BEFORE_BOX_CFG'] = set_log_before_cfg},
    }
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

local function assert_normal_exit(server)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    local msg = "can't initialize storage: Incorrect value for option " ..
                "'replication': failed to connect to one or more replicas"
    t.helpers.retrying({}, function()
        t.assert(server:grep_log(msg, nil, {filename = logfile}),
                 'Configuration is failed')
    end)
    t.assert_equals(server:grep_log('Assertion failed', nil,
                                    {filename = logfile}),
                    nil, 'Exit without a crash')
end

g.test_crash_without_self = function(cg)
    cg.server.box_cfg.replication = {
        server.build_listen_uri('nonexistent', cg.replica_set.id),
    }
    cg.server:start{wait_until_ready = false}
    assert_normal_exit(cg.server)
end

g.test_crash_with_self = function(cg)
    cg.server.box_cfg.replication = {
        server.build_listen_uri(cg.server.alias, cg.replica_set.id),
        server.build_listen_uri('nonexistent', cg.replica_set.id),
    }
    cg.server:start{wait_until_ready = false}
    assert_normal_exit(cg.server)
end
