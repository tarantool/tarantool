local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'fiber', require('fiber'))

        box.schema.space.create('s')
    end)
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.before_test('test_number', function(cg)
    cg.server:exec(function()
        box.space.s:create_index('p', {parts = {1, 'double'}})
    end)
end)

g.test_number = function(cg)
    cg.server:exec(function()
        local s = box.space.s

        local f1 = _G.fiber.new(function()
            box.atomic(function()
                s:get{0}
                _G.fiber.yield()
                s:replace{0}
            end)
        end)
        f1:set_joinable(true)

        local f2 = _G.fiber.new(function()
            box.atomic(function()
                s:get{require('ffi').cast('double', 0)}
                _G.fiber.yield()
                s:replace{0}
            end)
        end)
        f2:set_joinable(true)

        _G.fiber.yield()

        s:replace{0}

        local msg = "Transaction has been aborted by conflict"

        local ok, err = f1:join()
        t.assert_not(ok)
        t.assert_equals(err.message, msg)

        ok, err = f2:join()
        t.assert_not(ok)
        t.assert_equals(err.message, msg)
    end)
end

g.before_test('test_collation', function(cg)
    cg.server:exec(function()
        box.space.s:create_index('s', {parts = {1, 'str',
                                                collation = 'unicode_ci'}})
    end)
end)

g.test_collation = function(cg)
    cg.server:exec(function()
        local s = box.space.s

        local f1 = _G.fiber.new(function()
            box.atomic(function()
                s:get{'a'}
                _G.fiber.yield()
                s:replace{'a'}
            end)
        end)
        f1:set_joinable(true)

        local f2 = _G.fiber.new(function()
            box.atomic(function()
                s:get{'A'}
                _G.fiber.yield()
                s:replace{'a'}
            end)
        end)
        f2:set_joinable(true)

        _G.fiber.yield()

        s:replace{'a'}

        local msg = "Transaction has been aborted by conflict"

        local ok, err = f1:join()
        t.assert_not(ok)
        t.assert_equals(err.message, msg)

        ok, err = f2:join()
        t.assert_not(ok)
        t.assert_equals(err.message, msg)
    end)
end
