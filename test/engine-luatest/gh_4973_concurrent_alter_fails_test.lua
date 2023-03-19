local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {
    {engine = 'memtx'},
    {engine = 'vinyl'},
})

g.before_all(function(cg)
    t.skip_if(cg.params.engine == 'vinyl',
              "Vinyl doesn't support rebuilding primary key")
    cg.server = server:new({})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

g.before_each(function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        -- If some tuples are not referenced by primary index, they will be
        -- deleted by gc and we will catch segmentation fault on s:drop().
        collectgarbage("collect")
        box.space.test:drop()
    end)
end)

g.test_concurrent_alter = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local tuple_num = 10000
        local iteration_steps = 10
        local s = box.space.test
        for i = 1, tuple_num do
            s:replace{i, i, "old"}
        end
        local function do_random_updates()
            for _ = 1, iteration_steps do
                local idx = math.random(1, tuple_num)
                local action_num = math.random(1, 5)
                if action_num == 1 then
                    s:delete(idx)
                else
                    s:replace{idx, idx, "new"}
                end
            end
        end

        local alter_is_over = false
        local function alter()
            s.index.pk:alter({parts = {{field = 2, type = 'unsigned'}}})
            alter_is_over = true
        end

        local function disturb()
            while not alter_is_over do
                box.atomic(do_random_updates)
                fiber.sleep(0)
            end
        end

        local function collect_garbage()
            while not alter_is_over do
                fiber.sleep(0)
                collectgarbage("collect")
            end
        end

        local gc = fiber.new(collect_garbage)
        local disturber = fiber.new(disturb)
        local alterer = fiber.new(alter)

        gc:set_joinable(true)
        disturber:set_joinable(true)
        alterer:set_joinable(true)

        t.assert(alterer:join())
        t.assert(disturber:join())
        t.assert(gc:join())
    end)
end
