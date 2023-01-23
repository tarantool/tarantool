local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        alias   = 'default',
        box_cfg = {
            memtx_use_mvcc_engine = true,
        }
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.after_each(function()
    g.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Test right from the https://github.com/tarantool/tarantool/issues/8122.
g.test_simple_conflict = function()
    g.server:exec(function()
        box.cfg{txn_isolation = 'best-effort'}
        box.schema.space.create('test'):create_index('pk')
        box.space.test:replace{1}
        require('fiber').create(function() box.space.test:delete{1} end)
        box.space.test:delete{1} -- must not fail.
    end)
end

-- Test right from the https://github.com/tarantool/tarantool/issues/7202,
--  a bit shortened but very close to the original.
g.test_excess_conflict_secondary_original = function()
    g.server:exec(function()
        box.cfg{txn_isolation = 'read-committed'}
        local test = box.schema.create_space("test", {
            format = {{"id", type = "number"},
                      {"indexed", type = "string"}}
        })
        test:create_index("id", {unique = true, parts = {{1, "number"}}})
        test:create_index("indexed", {unique = false, parts = {{2, "string"}}})

        local fiber = require('fiber')

        local id = 0
        local nextId = function()
            id = id + 1;
            return id;
        end

        local fibers = {}
        local results = {}

        local put = function(fib_no)
            table.insert(fibers, fiber.self())
            fiber.self():set_joinable(true)
            box.begin()
            test:put({nextId(), "queue", "READY", "test" })
            results[fib_no] = pcall(box.commit)
        end

        local firstUpdate = function()
            table.insert(fibers, fiber.self())
            fiber.self():set_joinable(true)
            box.begin()
            for _, task in test.index.indexed:pairs() do
                test:update(task.id, { { '=', "indexed", "test + test" } })
            end
            results[3] = pcall(box.commit)
        end

        local secondUpdate = function()
            table.insert(fibers, fiber.self())
            fiber.self():set_joinable(true)
            results[4] = false
            box.begin()
            for _, task in test.index.indexed:pairs() do
                test:update(task.id, { { '=', "indexed", "test - test" } })
            end
            results[4] = pcall(box.commit)
        end

        for fib_no = 1, 2 do
            fiber.create(put, fib_no)
        end
        fiber.create(firstUpdate)
        fiber.create(secondUpdate)

        t.assert_equals(#fibers, 4)
        for _,f in pairs(fibers) do
            f:join()
        end

        t.assert_equals(results, {true, true, true, true})
        t.assert_equals(test:select{},
            {{1, 'test - test', 'READY', 'test'},
             {2, 'test - test', 'READY', 'test'}})
    end)
end

-- Checks that in case we find a deleted story during tuple clarification,
-- and this deleted story is prepared, we do not conflict the current
-- transaction, since we do not actually skip the story.
g.test_conflict_of_visible_deleted_secondary_index_story = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local test = box.schema.space.create('test')
        test:create_index('pk')
        test:create_index('sk', {parts = {{2}}})

        test:replace{0, 0}

        local first_replace = fiber.new(function()
            box.atomic(function()
                test:replace{0, 1}
            end)
        end)
        local second_replace = fiber.new(function()
            box.atomic(function()
                test.index[1]:get{0}
                test:replace {0, 0}
            end)
        end)

        first_replace:set_joinable(true)
        second_replace:set_joinable(true)

        t.assert(first_replace:join())
        t.assert(second_replace:join())
    end)
end
