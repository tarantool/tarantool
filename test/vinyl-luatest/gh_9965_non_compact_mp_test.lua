local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{datadir = 'test/vinyl-luatest/gh_9965_data'}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_bloom_integer = function(cg)
    cg.server:exec(function()
        local pk = box.space.test.index[0]
        local sk = box.space.test.index[1]
        local key1 = 99999999999998
        local key2 = 99999999999999

        t.assert_equals(pk:get{key1 - 1}, nil)
        t.assert_equals(pk:get{key1}, {key1, -key1})
        t.assert_equals(pk:get{key2}, {key2, -key2})
        t.assert_equals(pk:get{key2 + 1}, nil)

        t.assert_equals(sk:get{-key2 - 1}, nil)
        t.assert_equals(sk:get{-key1}, {key1, -key1})
        t.assert_equals(sk:get{-key2}, {key2, -key2})
        t.assert_equals(sk:get{-key1 + 1}, nil)
    end)
end

g.test_bloom_float = function(cg)
    cg.server:exec(function()
        box.cfg{vinyl_cache = 0}
        --
        -- gh-3907: check that integer numbers stored as MP_FLOAT/MP_DOUBLE
        -- are hashed as MP_INT/MP_UINT.
        --
        local ffi = require('ffi')
        local s = box.schema.space.create('test2', {engine = 'vinyl'})
        s:create_index('primary', {parts = {1, 'number'}})
        s:replace{ffi.new('double', 0)}
        s:replace{ffi.new('double', -1)}
        s:replace{ffi.new('double', 9007199254740992)}
        s:replace{ffi.new('double', -9007199254740994)}
        box.snapshot()
        t.assert_equals(s:get(0LL), {0})
        t.assert_equals(s:get(-1LL), {-1})
        t.assert_equals(s:get(9007199254740992LL), {9007199254740992})
        t.assert_equals(s:get(-9007199254740994LL), {-9007199254740994})
        s:drop()
    end)
end
