local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('gh-8704-empty-xlog-on-exit')

local fio = require('fio')

g.before_each(function(cg)
    cg.rs = replica_set:new{}
    cg.server = cg.rs:build_and_add_server{
        alias = 'server',
        box_cfg = {
            replication = server.build_listen_uri('nonexistent_uri', cg.rs.id),
        },
    }
end)

g.after_each(function(cg)
    cg.rs:drop()
end)

g.test_no_empty_xlog_on_startup_failure = function(cg)
    cg.server:start{wait_until_ready = false}
    local logfile = fio.pathjoin(cg.server.workdir, 'server.log')
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('connecting to 1 replicas', nil,
                                    {filename = logfile}),
                 'Server is started')
    end)
    cg.server:stop()
    local xlog_pattern = fio.pathjoin(cg.server.workdir, '*.xlog')
    t.assert_equals(#fio.glob(xlog_pattern), 0,
                    'No xlogs left after a failed startup')
    local snap_pattern = fio.pathjoin(cg.server.workdir, '*.snap')
    t.assert_equals(#fio.glob(snap_pattern), 0,
                    'No snapshots left after a failed startup')
end
