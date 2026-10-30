local server = require('luatest.server')
local t = require('luatest')

local g = t.group('space_truncate_under_load', t.helpers.matrix{
    engine = {'memtx', 'vinyl'},
})

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = true},
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_space_truncate_under_load = function(cg)
    cg.server:exec(function(params)
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = params.engine})
        s:create_index('primary')
        s:create_index('secondary', {
            unique = false,
            parts = {{2, 'unsigned'}},
        })

        for i = 1, 10 do
            s:replace({i, i * 10})
        end

        local cond = fiber.cond()
        local inprogress = {}
        for i = 1, 5 do
            local f = fiber.new(function()
                box.begin()
                s:replace({i, i * 100})
                s:replace({i + 10, i * 5})
                cond:wait()
                box.commit()
            end)
            f:set_joinable(true)
            table.insert(inprogress, f)
        end

        box.error.injection.set('ERRINJ_WAL_DELAY', true)

        local unconfirmed = {}
        for i = 5, 10 do
            local f = fiber.new(function()
                box.begin()
                s:replace({i, i * 100})
                s:replace({i + 10, i * 5})
                box.commit()
            end)
            f:set_joinable(true)
            table.insert(unconfirmed, f)
        end
        fiber.yield()

        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        s:truncate()

        t.assert_equals(s:select(), {})

        local function join(f)
            local ok, err = f:join(10)
            if not ok then
                error(err)
            end
        end

        cond:broadcast()
        for _, f in ipairs(inprogress) do
            t.assert_error_covers({
                type = 'ClientError',
                code = box.error.TRANSACTION_CONFLICT,
            }, join, f)
        end

        for _, f in ipairs(unconfirmed) do
            join(f)
        end

        t.assert_equals(s:select(), {})
    end, {cg.params})
end
