local it = require('test.interactive_tarantool')
local t = require('luatest')

local g = t.group()

-- Basic case: prompt is shown and hid.
--
-- It does not check that tarantool preserves current input line
-- and cursor position.
g.test_basic_print = function()
    local child = it.new({args = {'-l', 'fiber'}})

    child:execute_command([[
        _ = fiber.create(function()
            fiber.name('print_flood', {truncate = true})
            while true do
                print('flood')
                fiber.sleep(0.001)
            end
        end)
    ]])
    child:assert_empty_response()

    local exp_line = child:prompt() .. it.CR .. it.ERASE_IN_LINE .. 'flood'
    for _ = 1, 10 do
        child:assert_line(exp_line)
    end

    child:close()
end

-- The same as the basic case, but prints flood using logger to
-- stderr.
--
-- We don't check for presence of 'flood' lines in the log, but
-- verify that prompt on stdout is shown and hid.
g.test_basic_log = function()
    local child = it.new({args = {'-l', 'fiber', '-l', 'log'}})

    child:execute_command([[
        _ = fiber.create(function()
            fiber.name('log_flood', {truncate = true})
            while true do
                log.info('flood')
                fiber.sleep(0.001)
            end
        end)
    ]])
    child:assert_empty_response()

    local exp_data = child:prompt() .. it.CR .. it.ERASE_IN_LINE
    for _ = 1, 10 do
        child:assert_data(exp_data)
    end

    child:close()
end
