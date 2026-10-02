local t = require('luatest')
local server = require('luatest.server')

local g = t.group(nil, t.helpers.matrix({
    trigger = {'on_replace', 'before_replace'},
    result = {'return', 'lua_error', 'box_error'},
}))

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_trigger_stack = function(cg)
    cg.server:exec(function(trigger, result)
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local values = setmetatable({}, {__mode = 'v'})
        local calls = 0
        local message = 'func adapter failure'
        s[trigger](s, function(_, new)
            calls = calls + 1
            local value
            if result == 'box_error' then
                value = box.error.new(box.error.PROC_LUA, message)
            elseif result == 'lua_error' then
                value = setmetatable({}, {
                    __tostring = function() return message end,
                })
            elseif trigger == 'before_replace' then
                value = box.tuple.new{new[1], new[2] + 1}
            else
                value = {}
            end
            values[calls] = value
            if result ~= 'return' then
                error(value, 0)
            end
            return value
        end)
        local function replace(i)
            if result == 'return' then
                local expected = trigger == 'before_replace' and 43 or 42
                t.assert_equals(s:replace{i, 42}:totable(), {i, expected})
            else
                t.assert_error_msg_content_equals(message, s.replace, s,
                                                  {i, 42})
            end
        end
        -- Triggers borrow the fiber's Lua state, which differs from this
        -- coroutine's state. Returning from the coroutine would clear that
        -- borrowed stack and hide any leaked values.
        coroutine.wrap(function()
            for i = 1, 5 do
                replace(i)
            end
            t.assert_equals(calls, 5)
            collectgarbage('collect')
            collectgarbage('collect')
            t.assert_equals(next(values), nil)
        end)()
    end, {cg.params.trigger, cg.params.result})
end
