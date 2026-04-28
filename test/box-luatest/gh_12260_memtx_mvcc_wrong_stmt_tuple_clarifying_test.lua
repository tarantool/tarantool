local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-12260-memtx-mvcc-wrong-stmt-tuple-clarifying')

g.before_all(function()
    t.tarantool.skip_if_not_debug()
    g.server = server:new({
        box_cfg = {
            memtx_use_mvcc_engine = true,
            txn_isolation = 'read-confirmed',
        },
    })
    g.server:start()

    g.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
    end)
end)

g.after_each(function()
    g.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.space.test:truncate()
    end)
end)

g.after_all(function()
    g.server:drop()
end)

local cases = {
    {
        name = 'delete',
        stmt = 'box.space.test:delete{1}',
        result = {},
    },
    {
        name = 'update',
        stmt = "box.space.test:update({1}, {{'=', 2, 30}})",
        result = {{1, 30}},
    },
}

for _, case in ipairs(cases) do
    g['test_wrong_' .. case.name .. '_tuple_clarifying'] = function()
        g.server:exec(function(stmt, result)
            local fiber = require('fiber')

            box.space.test:insert{1}

            box.error.injection.set('ERRINJ_WAL_DELAY', true)
            local f1 = fiber.create(function()
                return box.space.test:replace{1}
            end)
            f1:set_joinable(true)

            local cond = fiber.cond()
            local f2 = fiber.create(function()
                box.begin()
                loadstring(stmt)()
                cond:wait()
                box.commit()
            end)
            f2:set_joinable(true)

            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.assert_equals(f1:join(), true)

            cond:signal()
            t.assert_equals(f2:join(), true)
            t.assert_equals(box.space.test:select{}, result)
        end, {case.stmt, case.result})
    end
end
