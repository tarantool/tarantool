local server = require('luatest.server')
local t = require('luatest')

-- Both engines to check if both luac and ffi implementations work correclty
local g = t.group(nil, {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
    g.server:exec(function(engine)
        local space = box.schema.space.create('test', {engine = engine})
        space:create_index('primary')
        space:insert({1})
        space:insert({2})
        space:insert({3})
    end, {g.params.engine})
end)

g.after_all(function(g)
    g.server:drop()
end)

-- Check if drop_while method works fine for space.pairs
g.test_drop_while = function(g)
    g.server:exec(function()
        local fun = require('fun')
        local select_result = box.space.test:select()

        local function check_case(cond)
            local actual = box.space.test:pairs():drop_while(cond):totable()
            local expected = fun.iter(select_result):drop_while(cond):totable()
            t.assert_equals(actual, expected)
        end

        check_case(function(_) return false end)
        check_case(function(_) return true end)
        check_case(function(t) return t[1] > 100 end)
        check_case(function(t) return t[1] < 100 end)
        check_case(function(t) return t[1] > 0 end)
        check_case(function(t) return t[1] < 0 end)
        check_case(function(t) return t[1] > 2 end)
        check_case(function(t) return t[1] < 2 end)
    end)
end

-- Check if method incompatible with stateful iterators are not supported
-- by space.pairs
g.test_disabled = function(g)
    g.server:exec(function()
        local methods = {'is_null', 'cycle'}

        for _, method in pairs(methods) do
            local iter = box.space.test:pairs()
            local errmsg = string.format(
                "stateful iterators do not support method '%s'", method)
            t.assert_error_msg_content_equals(errmsg, iter[method], iter)
        end
    end)
end
