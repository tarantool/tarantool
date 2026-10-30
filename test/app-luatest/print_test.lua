local it = require('test.interactive_tarantool')
local t = require('luatest')

local g = t.group()

g.after_each(function()
    if g.child ~= nil then
        g.child:close()
        g.child = nil
    end
end)

g.test_print_reaches_stdout = function()
    g.child = it.new()
    g.child:execute_command("print('Hello, world')")
    g.child:assert_line('Hello, world')
    g.child:assert_empty_response()
end

g.test_print_from_fiber_reaches_stdout = function()
    g.child = it.new({args = {'-l', 'fiber'}})
    g.child:execute_command([[
        _ = fiber.create(function()
            print('Ehllo, world')
        end)
    ]])
    -- The print() output goes first, then the empty response markers
    -- (`---` and `...`).
    g.child:read_until_line('Ehllo, world')
    g.child:assert_empty_response()
end
