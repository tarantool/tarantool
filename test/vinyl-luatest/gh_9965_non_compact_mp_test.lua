local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    --
    -- The disk data stores values hashed with a legacy function which used to
    -- be hashing numbers incorrectly. After it was fixed, the old indexes
    -- should still be accessible.
    --
    cg.server = server:new({
        datadir = 'test/vinyl-luatest/gh_9965_data',
        box_cfg = {
            -- Make vinyl use the bloom-filters forcefully, always.
            vinyl_cache = 0,
        }
    })
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'ffi', require('ffi'))
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_bloom_big_integer = function(cg)
    cg.server:exec(function()
        local idx_uint = box.space.test.index.idx_uint
        local idx_int = box.space.test.index.idx_int
        local key1 = 99999999999998
        local key2 = 99999999999999

        local cases = {
            {
                key = key1 - 1,
                idx = idx_uint,
            },
            {
                key = key1,
                idx = idx_uint,
                tuple = {'bigint1', key1, -key1, 0, 0},
            },
            {
                key = key2,
                idx = idx_uint,
                tuple = {'bigint2', key2, -key2, 0, 0},
            },
            {
                key = key2 + 1,
                idx = idx_uint,
            },
            {
                key = -key2 - 1,
                idx = idx_int,
            },
            {
                key = -key1,
                idx = idx_int,
                tuple = {'bigint1', key1, -key1, 0, 0},
            },
            {
                key = -key2,
                idx = idx_int,
                tuple = {'bigint2', key2, -key2, 0, 0},
            },
            {
                key = -key1 + 1,
                idx = idx_int,
            },
        }
        for _, case in pairs(cases) do
            t.assert_equals(case.idx:select{case.key}, {case.tuple})
        end
    end)
end

g.test_bloom_str = function(cg)
    cg.server:exec(function()
        local idx_str = box.space.test.index.idx_str
        t.assert_equals(idx_str:select{"123"}, {})
        t.assert_equals(idx_str:select{"double_int_as_double"},
                        {{'double_int_as_double', 0, 0, 11, 0}})
    end)
end

g.test_bloom_double = function(cg)
    cg.server:exec(function()
        local idx_dbl = box.space.test.index.idx_dbl

        local cases = {
            {
                key = 9,
            },
            {
                key = 10,
                tuple = {'double_int', 0, 0, 10, 0},
            },
            {
                key = 11,
                tuple = {'double_int_as_double', 0, 0, 11, 0},
            },
            {
                key = 12,
                tuple = {'double_int_as_float', 0, 0, 12, 0},
            },
            {
                key = 13,
            },
            {
                key = 13.5,
                tuple = {'double_double', 0, 0, 13.5, 0},
            },
            {
                key = 14,
            },
            {
                key = 14.5,
                tuple = {'double_float', 0, 0, 14.5, 0},
            },
        }
        for _, case in pairs(cases) do
            t.assert_equals(idx_dbl:select{case.key}, {case.tuple})
            t.assert_equals(idx_dbl:select{_G.ffi.cast('double', case.key)},
                            {case.tuple})
            t.assert_equals(idx_dbl:select{_G.ffi.cast('float', case.key)},
                            {case.tuple})
        end
    end)
end

g.test_bloom_number = function(cg)
    cg.server:exec(function()
        local idx_num = box.space.test.index.idx_num

        local cases = {
            {
                key = 9,
            },
            {
                key = 10,
                tuple = {'number_int', 0, 0, 0, 10},
            },
            {
                key = 11,
                tuple = {'number_int_as_double', 0, 0, 0, 11},
            },
            {
                key = 12,
                tuple = {'number_int_as_float', 0, 0, 0, 12},
            },
            {
                key = 13,
            },
            {
                key = 14,
            },
        }
        for _, case in pairs(cases) do
            t.assert_equals(idx_num:select{case.key}, {case.tuple})
            t.assert_equals(idx_num:select{_G.ffi.cast('double', case.key)},
                            {case.tuple})
            t.assert_equals(idx_num:select{_G.ffi.cast('float', case.key)},
                            {case.tuple})
        end
        --
        -- Double number in the old versions couldn't be found by a float
        -- number, regardless of their values.
        --
        local key = 13.5
        local tuple = {'number_double', 0, 0, 0, 13.5}
        t.assert_equals(idx_num:select{key}, {tuple})
        t.assert_equals(idx_num:select{_G.ffi.cast('double', key)}, {tuple})
        -- This vvv.
        t.assert_equals(idx_num:select{_G.ffi.cast('float', key)}, {})
        --
        -- And vice versa.
        --
        key = _G.ffi.cast('float', 14.5)
        tuple = {'number_float', 0, 0, 0, 14.5}
        t.assert_equals(idx_num:select{key}, {tuple})
        -- This vvv.
        t.assert_equals(idx_num:select{_G.ffi.cast('double', key)}, {})
        t.assert_equals(idx_num:select{_G.ffi.cast('float', key)}, {tuple})
    end)
end

g.test_bloom_hash_float_as_int_new_data = function(cg)
    cg.server:exec(function()
        box.cfg{vinyl_cache = 0}
        --
        -- gh-3907: check that integer numbers stored as MP_FLOAT/MP_DOUBLE
        -- are hashed as MP_INT/MP_UINT.
        --
        local s = box.schema.space.create('test2', {engine = 'vinyl'})
        s:create_index('primary', {parts = {1, 'number'}})
        s:replace{_G.ffi.new('double', 0)}
        s:replace{_G.ffi.new('double', -1)}
        s:replace{_G.ffi.new('double', 9007199254740992)}
        s:replace{_G.ffi.new('double', -9007199254740994)}
        box.snapshot()
        t.assert_equals(s:get(0LL), {0})
        t.assert_equals(s:get(-1LL), {-1})
        t.assert_equals(s:get(9007199254740992LL), {9007199254740992})
        t.assert_equals(s:get(-9007199254740994LL), {-9007199254740994})
        s:drop()
    end)
end
