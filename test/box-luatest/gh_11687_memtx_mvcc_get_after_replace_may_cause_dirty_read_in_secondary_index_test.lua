local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-11687-memtx-mvcc-get-after-replace-' ..
                  'may-cause-dirty-read-in-secondary-index')
--
-- gh-11687: memtx mvcc get-after-replace
-- may cause dirty read in secondary index
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

g.test_insert_after_delete = function()
    g.server:exec(function()
        box.space.test:replace{0, 2}

        local fiber = require('fiber')

        local cond = fiber.cond()
        local f = fiber.create(function()
            box.begin()
            box.space.test:replace{0, 1}
            t.assert_equals(box.space.test.index.sk:get{2}, nil)
            cond:wait()
            box.commit()
        end)
        f:set_joinable(true)

        box.begin()
        box.space.test:delete{0}
        box.space.test:insert{2, 2}
        box.commit()

        cond:signal()
        local _, err = f:join()
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.TRANSACTION_CONFLICT,
            message = 'Transaction has been aborted by conflict',
        })
    end)
end
