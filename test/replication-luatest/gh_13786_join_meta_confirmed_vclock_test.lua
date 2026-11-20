local t = require('luatest')
local server = require('luatest.server')
local g = t.group('gh_13786_join_meta_confirmed_vclock')

g.before_all(function(cg)
    cg.master = server:new({alias = 'master', box_cfg = {
        replication_synchro_quorum = 1,
    }})
    cg.master:start()
    cg.master:exec(function()
        box.ctl.promote()
        box.schema.space.create('s', {is_sync = true}):create_index('pk')
        box.space.s:insert{1}
    end)
    cg.replica = server:new({alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        read_only = true,
    }})
    cg.replica:start()
end)

g.after_all(function(cg)
    cg.replica:drop()
    cg.master:drop()
end)

g.test_promote_after_demote = function(cg)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.master:exec(function() box.ctl.demote() end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
    end)
    local term = cg.master:exec(function()
        box.ctl.promote()
        return box.info.election.term
    end)
    cg.replica:exec(function(term)
        t.helpers.retrying({timeout = 2}, function()
            t.assert_equals(box.info.synchro.queue.term, term)
            t.assert_equals(box.info.synchro.queue.owner, 1)
        end)
    end, {term})
end
