local t = require('luatest')
local server = require('luatest.server')

local g = t.group(nil, t.helpers.matrix{engine = {'memtx', 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = true},
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test1 ~= nil then
            box.space.test1:drop()
        end
        if box.space.test2 ~= nil then
            box.space.test2:drop()
        end
    end)
end)

g.test_ddl_does_not_abort_unrelated_transactions = function(cg)
    t.skip_if(cg.params.engine == 'memtx', 'gh-10377')
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        box.schema.create_space('test1', {engine = engine})
        box.space.test1:create_index('primary')
        box.begin()
        box.space.test1:insert({1, 10})
        local f = fiber.new(function()
            box.schema.create_space('test2', {engine = engine})
            box.space.test2:create_index('primary')
        end)
        f:set_joinable(true)
        t.assert_equals({f:join()}, {true})
        t.assert_equals({pcall(box.commit)}, {true})
        t.assert_equals(box.space.test1:select(), {{1, 10}})
    end, {cg.params.engine})
end

g.test_ddl_aborts_related_transactions = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        box.schema.create_space('test1', {engine = engine})
        box.space.test1:create_index('primary')
        box.begin()
        box.space.test1:insert({1, 10})
        local f = fiber.new(function()
            box.space.test1:create_index('secondary', {parts = {2, 'unsigned'}})
        end)
        f:set_joinable(true)
        t.assert_equals({f:join()}, {true})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.TRANSACTION_CONFLICT,
        }, box.commit)
        t.assert_equals(box.space.test1:select(), {})
    end, {cg.params.engine})
end
