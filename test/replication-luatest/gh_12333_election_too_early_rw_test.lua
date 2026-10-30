local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

--
-- gh-12333: there was a moment when a new leader got elected, but didn't
-- get promoted yet. Then its election-role is a leader, while the synchro
-- queue looks entirely idle and unclaimed. There was a bug when this state was
-- reported as writable.
--
g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master = server:new{alias = 'master'}
    cg.master:start()
end)

g.after_all(function(cg)
    cg.master:drop()
end)

g.test_case = function(cg)
    cg.master:exec(function()
        box.cfg{election_mode = 'manual'}
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', true)
        local f = require('fiber').new(box.ctl.promote)
        f:set_joinable(true)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.election.state, 'leader')
        end)
        t.assert(box.info.ro)
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', false)
        local ok, err = f:join(120)
        t.assert_equals(err, nil)
        t.assert(ok)
        t.assert_not(box.info.ro)
    end)
end
