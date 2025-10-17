local server = require('luatest.server')
local t = require('luatest')

local g =
    t.group('gh-11927-mvcc-false-positive-rollbacks-of-identical-replaces')
--
-- gh-11927: mvcc false-positive rollbacks of identical replaces
--

g.before_all(function()
    g.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
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

g.test_false_positive_rollbacks_of_identical_replaces = function()
    g.server:exec(function()
        local fiber = require('fiber')

        local cond1 = fiber.cond()
        local f1 = fiber.create(function()
            box.begin()
            box.space.test:replace{1, 1}
            cond1:wait()
            box.commit()
        end)
        f1:set_joinable(true)

        local cond2 = fiber.cond()
        local f2 = fiber.create(function()
            box.begin()
            box.space.test:replace{1, 1}
            box.space.test:replace{1, 1}
            cond2:wait()
            box.commit()
        end)
        f2:set_joinable(true)

        cond1:signal()
        local ok = f1:join()
        t.assert(ok)

        cond2:signal()
        local ok = f2:join()
        t.assert(ok)
    end)
end
