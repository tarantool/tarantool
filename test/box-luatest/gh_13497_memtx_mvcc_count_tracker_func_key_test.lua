local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13497-memtx-mvcc-count-tracker-func-key')

g.before_all(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)
-- Check that a count tracker bounded by a functional index entry survives the
-- replacement of the functional key of the bound.
g.test_count_tracker_bound_func_key_replaced = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        box.schema.func.create('g', {
            body = 'function(t) return {t[2] % 5, t[3]} end',
            is_deterministic = true, is_sandboxed = true,
        })
        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:create_index('gk', {
            parts = {{1, 'unsigned'}, {2, 'unsigned'}}, func = 'g',
        })
        for i = 1, 5 do
            s:replace{i, i, i % 3}
        end

        -- The offset select stops at the functional key {3, 0} of {3, 3, 0}
        -- and leaves a count tracker bounded by it.
        local ch = fiber.channel()
        local f = fiber.create(function()
            box.begin()
            t.assert_equals(s.index.gk:select({}, {
                iterator = 'LE', offset = 1, limit = 2,
            }), {{3, 3, 0}, {2, 2, 2}})
            ch:get()
            box.commit()
        end)
        f:set_joinable(true)

        -- The update frees the functional key of the bound.
        s:update({3}, {{'=', 2, 8}})
        -- A write matching the count tracker is compared with the bound.
        s:replace{5, 4, 3}

        ch:put(true)
        t.assert(f:join())
        s:drop()
        box.func.g:drop()
    end)
end
