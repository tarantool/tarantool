local server = require('luatest.server')
local t = require('luatest')
local fio = require('fio')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({
        box_cfg = {
            force_recovery = true,
        },
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- gh-167: force recovery must not get stuck on a gap in the LSN sequence caused
-- by a missing xlog. A normal recovery must stop at such an inconsistency;
-- force recovery reports the gap to the log and still applies the rows stored
-- after it.
g.test_force_recovery_ignores_lsn_gap = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary')
        s:insert({1, 'first tuple'})
        s:insert({2, 'second tuple'})
    end)

    -- The next xlog to be opened (on the restart below) will hold the {3}
    -- insert. Remember its path so it can be removed later to make the gap.
    local lsn = cg.server:exec(function() return box.info.lsn end)
    local wal = fio.pathjoin(cg.server.workdir,
                             string.format('%020d.xlog', lsn))

    -- Put {3} and {4} into their own xlogs by restarting in between.
    cg.server:restart()
    cg.server:exec(function()
        box.space.test:insert({3, 'third tuple'})
    end)
    cg.server:restart()
    cg.server:exec(function()
        box.space.test:insert({4, 'fourth tuple'})
    end)

    -- Drop the xlog that holds {3} and recover from the resulting gap.
    cg.server:stop()
    t.assert_equals(#fio.glob(wal), 1, 'the xlog to remove exists')
    fio.unlink(wal)
    cg.server:start()

    t.assert(cg.server:grep_log('ignoring a gap in LSN'))

    -- The tuple from the removed xlog is lost, the rest is recovered.
    cg.server:exec(function()
        t.assert_equals(box.space.test:select(), {
            {1, 'first tuple'},
            {2, 'second tuple'},
            {4, 'fourth tuple'},
        })
    end)
end
