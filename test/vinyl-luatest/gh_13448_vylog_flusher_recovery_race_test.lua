local server = require('luatest.server')
local fio = require('fio')
local t = require('luatest')

local g = t.group()

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g.test_vylog_recovery_flusher_race = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        --
        -- Create more vinyl spaces so that test10 truncation is written before
        -- test10 creation on recovery.
        --
        for i = 1, 10 do
            local s = box.schema.space.create('test' .. i, {engine = 'vinyl'})
            s:create_index('pk')
        end
        box.space.test10:insert({1})
        box.space.test1:insert({1, string.rep('a', 10000)})
        box.space.test10:truncate()
    end)
    --  Delete vylog imitating restoring after backup.
    local vylog = fio.pathjoin(cg.server.workdir, '00000000000000000000.vylog')
    t.assert(fio.unlink(vylog))
    --
    -- Tweak so that recovery yields before truncation and flusher starts to
    -- write vylog.
    --
    -- Delay writing vylog entries so that VY_LOG_DROP_LSM is written on
    -- finishing recovery before entries creating space are written by flusher.
    --
    local before_box_cfg = [[
        local tweaks = require('internal.tweaks')
        tweaks.xlog_row_bytes_per_yield = 5000
        box.error.injection.set('ERRINJ_VY_LOG_DELAY_DURATION', 0.1)
    ]]
    -- First restart writes incorrect vylog.
    cg.server:restart({env = {TARANTOOL_RUN_BEFORE_BOX_CFG = before_box_cfg}})
    -- Second restart would fail without fix.
    cg.server:restart()
end
