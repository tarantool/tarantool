local it = require('test.interactive_tarantool')

local t = require('luatest')
local g = t.group()

-- Checks that `print` throwing an error is handled correctly.
g.test_basic_print_with_exception = function()
    local child = it.new({args = {'-l', 'fiber'}})

    child:execute_command([[
        _ = fiber.create(function()
            fiber.name('print_flood', {truncate = true})
            while true do
                pcall(function()
                          print(setmetatable({}, {__tostring = error}))
                      end)
                print('flood')
                fiber.sleep(0.01)
            end
        end)
    ]])
    child:assert_empty_response()

    local exp_line = it.PROMPT .. it.CR .. it.ERASE_IN_LINE ..
                     it.PROMPT .. it.CR .. it.ERASE_IN_LINE ..
                     'flood'
    for _ = 1, 10 do
        child:assert_line(exp_line)
    end

    child:close()
end
