local fiber = require('fiber')
-- -------------------------------------------------------------------------- --
-- Local functions
-- -------------------------------------------------------------------------- --

-- printer task fiber
local printer_task

-- tester task fiber
local tester_task

-- test log
local result = {}

-- -------------------------------------------------------------------------- --
-- printer task routines
-- -------------------------------------------------------------------------- --

-- odd printer
local function odd(x)
	table.insert(result,'A: odd  '..tostring(x))
    fiber.sleep(0.0)
	table.insert(result,'B: odd  '..tostring(x))
end

-- even printer
local function even(x)
	table.insert(result,'C: event  '..tostring(x))
    if x == 2 then
        return
    end
	table.insert(result,'D: event  '..tostring(x))
end

-- printer task routine main function
local function printer_task_routine(x)
    for i = 1, x do
        if i == 3 then
            fiber.sleep(0)
        end
        if i % 2 == 0 then
            even(i)
        else
            odd(i)
        end
    end
end


------------------------------------------------------------------------
-- tester task routines
------------------------------------------------------------------------

-- tester task routine main function
local function tester_task_routine()
    printer_task = fiber.create(printer_task_routine, 5)
	table.insert(result, "tester: status(printer) = " .. printer_task:status())
    local count = 1
    while printer_task:status() ~= "dead" do
		table.insert(result, "count: " .. tostring(count))
		table.insert(result, "status: " .. printer_task:status())
        count = count + 1
        fiber.sleep(0)
    end
end


-- -------------------------------------------------------------------------- --
-- Test functions
-- -------------------------------------------------------------------------- --

-- run fiber test
local function box_fiber_run_test()
    -- run tester
    tester_task = fiber.create(tester_task_routine)
    while tester_task:status() ~= 'dead' do
        fiber.sleep(0)
    end
	return result
end

return box_fiber_run_test
