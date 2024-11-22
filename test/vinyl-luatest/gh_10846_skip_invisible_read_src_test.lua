local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_skip_invisible_read_src = function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', true)

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        local i = s:create_index('primary')

        local function write(c)
            box.begin()
            for i = 101, 200 do
                s:replace{i, c}
            end
            box.commit()
        end

        write(1)
        box.snapshot()
        write(2)

        local gen, param, state = i:pairs()
        local _, tuple = gen(param, state)
        t.assert_equals(tuple, {101, 2})

        t.assert_covers(i:stat(), {
            range_count = 1,
            run_count = 1,
            memory = {iterator = {lookup = 1, get = {rows = 1}}},
            disk = {iterator = {lookup = 1, get = {rows = 1}}},
        })

        box.snapshot()
        write(3)
        box.snapshot()
        write(4)

        box.stat.reset()

        -- The iterator must be sent to a read view.
        local _, tuple = gen(param, state)
        t.assert_equals(tuple, {102, 2})

        -- The iterator must skip the memory level and the most recent run
        -- because they were created after the read view.
        t.assert_covers(i:stat(), {
            range_count = 1,
            run_count = 3,
            memory = {iterator = {lookup = 0, get = {rows = 0}}},
            disk = {iterator = {lookup = 2, get = {rows = 2}}},
        })
    end)
end
