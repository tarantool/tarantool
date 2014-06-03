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
    fiber.yield(x)
	table.insert(result,'B: odd  '..tostring(x))
end

-- even printer
local function even(x)
	table.insert(result,'C: event  '..tostring(x))
    if x == 2 then
        return x
    end
	table.insert(result,'D: event  '..tostring(x))
end

-- printer task routine main function
local function printer_task_routine(x)
	table.insert(result, "printer: tester status = " .. fiber.status(tester_task))
	table.insert(result, "printer: printer status = " .. fiber.status(printer_task))
    for i = 1, x do
        if i == 3 then
            fiber.yield(-1)
        end
        if i % 2 == 0 then
            even(i)
        else
            odd(i)
        end
    end
end


-- -------------------------------------------------------------------------- --
-- tester task routines
-- -------------------------------------------------------------------------- --

-- tester task routine main function
local function tester_task_routine()
    printer_task = fiber.create(printer_task_routine)
	table.insert(result, "tester: status(tester) = " .. fiber.status(tester_task))
	table.insert(result, "tester: status(printer) = " .. fiber.status(printer_task))
    count = 1
    while fiber.status(printer_task) ~= "dead" do
		table.insert(result, "count: " .. tostring(count))
        fiber.resume(printer_task, 5)
		table.insert(result, "status: " .. fiber.status(printer_task))
        count = count + 1
    end
end


-- -------------------------------------------------------------------------- --
-- Test functions
-- -------------------------------------------------------------------------- --

-- run fiber test
function box_fiber_run_test()
    -- run tester
    tester_task = fiber.create(tester_task_routine)
    fiber.resume(tester_task)
	return result
end
