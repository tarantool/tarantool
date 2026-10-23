local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_pagination_start_gap_tracking = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('sk', {unique = false,
                              parts = {{2, 'unsigned'}}})
        s:replace{10, 3}
        s:replace{20, 3}
        s:replace{30, 3}

        box.begin()
        -- The reader resumes among the duplicates of 3 and
        -- observes the gap between (3, 20) and (3, 30).
        t.assert_equals(
            s.index.sk:select({3}, {iterator = 'GE',
                                    after = {20, 3}, limit = 1}),
            {{30, 3}})
        s:replace{7, 7}

        -- A concurrent insert lands inside the observed gap.
        fiber.create(function()
            s:replace{25, 3}
        end)

        -- The transaction must be aborted by conflict.
        t.assert_error_covers({
            code = box.error.TRANSACTION_CONFLICT,
        }, box.commit)
    end)
end
