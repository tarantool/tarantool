local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-11927-memtx-mvcc-reduce-false-positive-aborts')
--
-- gh-11927: memtx mvcc reduce false-positive aborts
--

g.before_all(function()
    g.server = server:new{
        box_cfg = {
            memtx_use_mvcc_engine = true
        }
    }
    g.server:start()

    g.server:exec(function()
        box.schema.space.create("test")
        box.space.test:format{{'a', type='unsigned'}, {'b', type='unsigned'}}
        box.space.test:create_index("pk", {parts={{'a'}}})
        box.space.test:create_index("sk", {parts={{'b'}}, unique=true})
    end)
end)

g.after_each(function()
    g.server:exec(function() box.space.test:truncate() end)
end)

g.after_all(function()
    g.server:drop()
end)

for _, case in ipairs({
    -- gh-11927
    {
        name = 'false_positive_aborts_of_identical_replaces',
        tuples = { {1, 1}, {1, 1}, {1, 1} },
    },
    -- gh-11942
    {
        name = 'avoidable_false_positive_transaction_aborts',
        tuples = { {1, 2}, {1, 1}, {2, 2} },
    },
}) do
    g['test_' .. case.name] = function()
        g.server:exec(function(tuples)
            local fiber = require('fiber')

            local c1 = fiber.cond()
            local f1 = fiber.create(function()
                box.begin()
                box.space.test:replace(tuples[1])
                c1:wait()
                box.commit()
            end)
            f1:set_joinable(true)

            local c2 = fiber.cond()
            local f2 = fiber.create(function()
                box.begin()
                box.space.test:replace(tuples[2])
                box.space.test:replace(tuples[3])
                c2:wait()
                box.commit()
            end)
            f2:set_joinable(true)

            c1:signal()
            local ok = f1:join()
            t.assert(ok)

            c2:signal()
            ok = f2:join()
            t.assert(ok)
        end, { case.tuples })
    end
end
