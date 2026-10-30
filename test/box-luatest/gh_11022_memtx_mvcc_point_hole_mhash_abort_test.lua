local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

-- The test catches a moment when mhash is being incrementally resized
-- and deletes a point hole tracker then - it caused crash because we have
-- deleted the item and only then removed it from the mhash. If incremental
-- resize is in progress, the element could be still used to calculate bucket
-- of the element in the shadow hash table.
g.test_point_holes_mhash_abort = function()
    g.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('test')
        s:create_index('pk')

        -- Two cursors - for gets and for replaces
        local get_i = 1
        local replace_i = 1

        -- On each step, get two times and replace one time.
        -- Thus, the hash table of point hole items will grow
        -- by 1 on each step - each get populates it with one more
        -- item, and each replace deletes one.
        -- Also, we will definitely catch a moment when a point hole item
        -- is deleted during the resize because we delete them as often
        -- as possible.
        fiber.set_max_slice(30)
        box.begin()
        for _ = 1, 10000 do
            s:get{get_i}
            get_i = get_i + 1
            s:get{get_i}
            get_i = get_i + 1
            s:replace{replace_i}
            replace_i = replace_i + 1
        end
        box.commit()
    end)
end
