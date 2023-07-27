local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('gh-8746-transaction-boundaries')

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

local function prepare(cg)
    cg.replica_set:start()
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.schema.space.create('loc', {is_local = true})
        box.space.loc:create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        box.space.test:on_replace(function(_, new)
            box.space.loc:replace(new)
        end)
    end)
    cg.master:exec(function()
        box.space.test:replace{1}
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end

g.before_test('test_replica_recovery', function(cg)
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.1,
        },
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            read_only = true,
            replication = server.build_listen_uri('master', cg.replica_set.id),
            replication_timeout = 0.1,
        },
    }
end)

g.test_replica_recovery = function(cg)
    prepare(cg)
    cg.replica:restart()
    cg.replica:wait_for_vclock_of(cg.master)
end
