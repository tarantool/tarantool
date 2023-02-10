local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {{disable_ffi = true}, {disable_ffi = false}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(disable_ffi)
        local s = box.schema.space.create('test')
        s:create_index('primary')
        s:create_index('secondary', {unique = false, parts = {2, 'unsigned'}})
        for i = 1, 5 do
            s:insert({i * 2 - 1, i})
            s:insert({i * 2, 6 - i})
        end
        if disable_ffi then
            local ffi = require('ffi')
            ffi.cdef('void box_read_ffi_disable(void);')
            ffi.C.box_read_ffi_disable()
        end
    end, {cg.params.disable_ffi})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_min = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        t.assert_error_msg_content_equals(
            "Use index:min(...) instead of index.min(...)",
            s.index.primary.min)
        t.assert_equals(s.index.primary:min(), {1, 1})
        t.assert_equals(s.index.primary:min(2), {2, 5})
        t.assert_equals(s.index.secondary:min(), {1, 1})
        t.assert_equals(s.index.secondary:min(2), {3, 2})
    end)
end

g.test_max = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        t.assert_error_msg_content_equals(
            "Use index:max(...) instead of index.max(...)",
            s.index.primary.max)
        t.assert_equals(s.index.primary:max(), {10, 1})
        t.assert_equals(s.index.primary:max(2), {2, 5})
        t.assert_equals(s.index.secondary:max(), {9, 5})
        t.assert_equals(s.index.secondary:max(2), {8, 2})
    end)
end

g.test_random = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        t.assert_error_msg_content_equals(
            "Use index:random(...) instead of index.random(...)",
            s.index.primary.random)
        t.assert_equals(s.index.primary:random(1), {2, 5})
        t.assert_equals(s.index.primary:random(2), {3, 2})
        t.assert_equals(s.index.secondary:random(3), {8, 2})
        t.assert_equals(s.index.secondary:random(4), {5, 3})
    end)
end

g.test_get = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        t.assert_error_msg_content_equals(
            "Use index:get(...) instead of index.get(...)",
            s.index.primary.get)
        t.assert_error_msg_content_equals(
            "Invalid key part count in an exact match in space \"test\"," ..
            " index \"primary\" (expected 1, got 0)",
            s.index.primary.get, s.index.primary)
        t.assert_error_msg_content_equals(
            "Get() doesn't support partial keys and non-unique indexes",
            s.index.secondary.get, s.index.secondary, 1)
        t.assert_equals(s:get(1), {1, 1})
        t.assert_equals(s.index.primary:get(1), {1, 1})
    end)
end

g.test_select = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        t.assert_error_msg_content_equals(
            "Use index:select(...) instead of index.select(...)",
            s.index.primary.select)
        t.assert_error_msg_content_equals(
            "Unknown iterator type 'foo'",
            s.index.primary.select, s.index.primary,  {}, {iterator = 'foo'})
        t.assert_equals(s:select(2, {iterator = 'lt'}), {{1, 1}})
        t.assert_equals(s.index.primary:select(1), {{1, 1}})
        t.assert_equals(s.index.secondary:select(1), {{1, 1}, {10, 1}})
    end)
end

g.test_pairs = function(cg)
    cg.server:exec(function()
        local fun = require('fun')
        local s = box.space.test
        t.assert_error_msg_content_equals(
            "Use index:pairs(...) instead of index.pairs(...)",
            s.index.primary.pairs)
        t.assert_error_msg_content_equals(
            "Unknown iterator type 'foo'",
            s.index.primary.pairs, s.index.primary,  {}, {iterator = 'foo'})
        local gen = s.index.secondary:pairs(2)
        t.assert_error_msg_content_equals("usage: next(param, state)", gen)
        t.assert_equals(fun.totable(s:pairs(2, {iterator = 'lt'})), {{1, 1}})
        t.assert_equals(fun.totable(s.index.primary:pairs(1)), {{1, 1}})
        t.assert_equals(fun.totable(s.index.secondary:pairs(1)),
                        {{1, 1}, {10, 1}})
    end)
end
