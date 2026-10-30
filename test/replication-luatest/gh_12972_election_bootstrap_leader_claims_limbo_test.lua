local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

--
-- gh-12972: when the bootstrap leader was elected during box.cfg(), the cfg
-- returned right after the Raft elections, without waiting for the synchro
-- queue claim. The caller of box.cfg() could observe the instance being an
-- election leader with the queue still unclaimed and the instance read-only,
-- regardless of the box.cfg.read_only setting.
--
-- It is actually expected that the limbo is claimed by the bootstrap leader.
-- The leader needs that, at least to grant privileges, create spaces, etc. It
-- was eventually promoted, but there is no sense in having a read-only time
-- gap after box.cfg().
--
local function before_box_cfg()
    local fiber = require('fiber')
    box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY_COUNTDOWN', 0)
    local watcher = fiber.new(function()
        -- Wait until the instance wins Raft elections, but block its attempt to
        -- claim the limbo.
        while true do
            while not box.error.injection.get(
                    'ERRINJ_TXN_LIMBO_BEGIN_DELAY') do
                fiber.sleep(0.001)
            end
            local ok, info = pcall(function()
                return box.info.election
            end)
            if ok and info.state == 'leader' then
                break
            end
            box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', false)
        end
        -- Give it time to return from box.cfg{} in the potential
        -- limbo-unclaimed bad state.
        --
        -- A hardcoded 'best effort' timeout is the only way to test this here.
        --
        -- In the broken code box.cfg{} would indeed return regardless of this
        -- fiber, since it wouldn't wait for the limbo claim.
        --
        -- In the fixed code box.cfg{} simply won't return until the injection
        -- is removed. And its immediate removal would make the broken code also
        -- pass.
        --
        -- This timeout is big enough so that the broken code has time to leave
        -- box.cfg() too early, and is small enough not to block box.cfg() for
        -- too long when the bug is fixed.
        --
        fiber.sleep(0.1)
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', false)
    end)
    watcher:set_joinable(true)
    local orig_cfg = box.cfg
    box.cfg = function(cfg)
        local res = orig_cfg(cfg)
        rawset(_G, 'at_cfg_return', {
            election_state = box.info.election.state,
            queue_owner = box.info.synchro.queue.owner,
        })
        watcher:join()
        return res
    end
end

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master = server:new{
        alias = 'master',
        box_cfg = {
            election_mode = 'candidate',
        },
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG =
                ('loadstring(%q)()'):format(string.dump(before_box_cfg)),
        },
    }
    cg.master:start()
end)

g.after_all(function(cg)
    cg.master:drop()
end)

g.test_case = function(cg)
    cg.master:exec(function()
        local state = rawget(_G, 'at_cfg_return')
        t.assert_not_equals(state, nil)
        t.assert_equals(state.election_state, 'leader')
        t.assert_equals(state.queue_owner, box.info.id)
    end)
end
