local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new {
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that preparation of an insert statement with an older story deleted by
-- a prepared transaction does not fail assertion.
g.test_preparation_with_deleted_older_story_assertion = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local t = require('luatest')

        box.space.s:replace{0}

        local first_replace = fiber.create(function()
            fiber.self():set_joinable(true)
            box.atomic(function()
                box.space.s:delete{0}
            end)
        end)
        local second_replace = fiber.create(function()
            fiber.self():set_joinable(true)
            box.atomic(function()
                box.space.s:insert{0}
            end)
        end)

        first_replace:join()
        second_replace:join()

        t.assert_equals(box.space.s:select{}, {{0}})
    end)
end
