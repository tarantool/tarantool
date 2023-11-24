local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh-9401-anon-replica-synchro')

--
-- gh-9401: Anonymous replica didn't receive a synchronous queue snapshot during
-- join, and treated every synchronous request as erroneous.
--
g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.1,
        },
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = server.build_listen_uri('master', cg.replica_set.id),
            replication_anon = true,
            replication_timeout = 0.1,
            read_only = true,
        },
    }
    cg.master:start()
    cg.master:exec(function()
        box.ctl.promote()
    end)
    cg.replica:start()
    cg.replica:assert_follows_upstream(cg.master:get_instance_id())
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_anon_replica_synchro_queue = function(cg)
    local old_term = cg.replica:exec(function(id)
        t.assert_equals(box.info.synchro.queue.owner, id)
        t.assert(box.info.synchro.queue.term > 1)
        return box.info.synchro.queue.term
    end, {cg.master:get_instance_id()})
    cg.master:exec(function()
        box.ctl.demote()
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    cg.replica:exec(function(term)
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert(box.info.synchro.queue.term > term)
    end, {old_term})
end

g.test_anon_replica_synchro_write = function(cg)
    cg.master:exec(function()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
        box.space.test:insert{1}
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    cg.replica:exec(function()
        t.assert_equals(box.space.test:get{1}, {1})
    end)
end
