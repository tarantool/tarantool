local server = require('luatest.server')
local t = require('luatest')

local g = t.group('main', t.helpers.matrix({
    force_recovery = {false, true},
    memtx_use_mvcc_engine = {false, true},
    memtx_use_sort_data = {false, true},
}))

local disable_triggers = [[
    local compat = require('compat')
    compat.box_recovery_triggers_deprecation = 'new'
]]

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {
            memtx_use_sort_data = true,
        },
    })
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:create_index('sk', {parts = {{2, 'unsigned'}}})
        box.begin()
        for i = 1, 1000 do
            s:insert({i, 1000 + i})
        end
        box.commit()
        box.snapshot()
    end)
end)

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
end)

g.test_snapshot_recovery_without_txns = function(cg)
    cg.server:restart({
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = disable_triggers},
        box_cfg = {
            force_recovery = cg.params.force_recovery,
            memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine,
            memtx_use_sort_data = cg.params.memtx_use_sort_data,
        },
    })
    cg.server:exec(function()
        t.assert_equals(box.space.test:count(), 1000)
    end)
end
