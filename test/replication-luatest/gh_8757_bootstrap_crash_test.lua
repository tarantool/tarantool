local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('gh-8757-bootstrap-crash')

local fio = require('fio')
local fiber = require('fiber')

local function make_uuid(char)
    local format_string = 'xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx'
    return string.gsub(format_string, 'x', char)
end

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.before_test('test_bootstrap_crash', function(cg)
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('leader', cg.replica_set.id),
            server.build_listen_uri('replica1', cg.replica_set.id),
            server.build_listen_uri('replica2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        instance_uuid = make_uuid('1'),
    }
    cg.leader = cg.replica_set:build_and_add_server{
        alias = 'leader',
        box_cfg = cg.box_cfg,
    }
    for i = 1, 2 do
        cg.box_cfg.instance_uuid = make_uuid(1 + i)
        cg['replica' .. i] = cg.replica_set:build_and_add_server{
            alias = 'replica' .. i,
            box_cfg = cg.box_cfg,
        }
    end
end)

g.test_bootstrap_crash = function(cg)
    cg.leader:start{wait_until_ready = false}
    cg.replica1:start{wait_until_ready = false}
    local logfile = fio.pathjoin(cg.leader.workdir, 'leader.log')
    local uuid_pattern = string.gsub(make_uuid(2), '%-', '%%-')
    local query = 'remote master ' .. uuid_pattern
    t.helpers.retrying({}, function()
        t.assert(cg.leader:grep_log(query, nil, {filename = logfile}),
                 'Leader sees replica1')
    end)
    fiber.sleep(0.1)
    cg.replica1:stop()
    cg.replica2:start{wait_until_ready = false}
    query = "Can't check who replica " .. uuid_pattern ..
            " at .* chose its bootstrap leader"
    t.helpers.retrying({}, function()
        t.assert(cg.leader:grep_log(query, nil, {filename = logfile}),
                 'Leader exits with an error')
    end)
end
