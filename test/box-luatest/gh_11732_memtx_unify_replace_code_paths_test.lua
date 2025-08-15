local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('p')
        s:create_index('s1', {parts = {2}})
        s:create_index('s2', {parts = {3}})

        s:insert{0, 0, 0}

        box.internal.memtx_tx_gc(100)
    end)
end)

g.after_each(function(cg)
    cg.server:drop()
end)

local function table_values_are_zeros(table)
    for _, v in pairs(table) do
        if type(v) ~= 'table' then
            if v ~= 0 then
                return false
            end
        else
            if not table_values_are_zeros(v) then
                return false
            end
        end
    end
    return true
end

g.test_check_dup_failures = function(cg)
    local stats = cg.server:exec(function()
        local s = box.space.s

        local msg = 'Duplicate key exists in unique index "p" in space "s" ' ..
            'with old tuple - [0, 0, 0] and new tuple - [0, 1, 1]'
        t.assert_error_msg_equals(msg, function()
            s:insert{0, 1, 1}
        end)
        t.assert_equals(s:get{0}, {0, 0, 0})

        msg = 'Duplicate key exists in unique index "s1" in space "s" ' ..
            'with old tuple - [0, 0, 0] and new tuple - [1, 0, 1]'
        t.assert_error_msg_equals(msg, function()
            s:insert{1, 0, 1}
        end)
        t.assert_equals(s:get{0}, {0, 0, 0})

        msg = 'Duplicate key exists in unique index "s2" in space "s" ' ..
            'with old tuple - [0, 0, 0] and new tuple - [1, 1, 0]'
        t.assert_error_msg_equals(msg, function()
            s:insert{1, 1, 0}
        end)
        t.assert_equals(s:get{0}, {0, 0, 0})

        box.internal.memtx_tx_gc(100)
        t.assert_equals(s:get{0}, {0, 0, 0})

        return box.stat.memtx.tx()
    end)
    t.assert(table_values_are_zeros(stats))
end

g.test_index_insert_failures = function(cg)
    t.tarantool.skip_if_not_debug()

    local stats = cg.server:exec(function()
        local s = box.space.s

        local msg = 'Failed to allocate 0 bytes in errinj for errinj'
        for i = 0, 2 do
            box.error.injection.set('ERRINJ_INDEX_OOM_COUNTDOWN', i)
            t.assert_error_msg_equals(msg, function()
                s:insert{1, 1, 1}
            end)
            box.error.injection.set('ERRINJ_INDEX_OOM', false)

            t.assert_equals(s:get{0}, {0, 0, 0})
        end

        box.internal.memtx_tx_gc(100)
        t.assert_equals(s:get{0}, {0, 0, 0})

        return box.stat.memtx.tx()
    end)
    t.assert(table_values_are_zeros(stats))
end
