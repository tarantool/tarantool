#!/usr/bin/env tarantool

-- gh-4231: run box_load_and_execute() (it is box.execute value
-- before box will be loaded) in several fibers in parallel and
-- ensure that it returns correct results (i.e. that the function
-- waits until box will be fully configured).

local fiber = require('fiber')
local tap = require('tap')

local box_load_and_execute = box.execute
local fiber_count = 10
local results = fiber.channel(fiber_count)

local function select_from_vindex()
    local res = box_load_and_execute('SELECT * FROM "_vindex"')
    results:put(res)
end

local test = tap.test('gh-4231-box-execute-locking')

test:plan(fiber_count)

local exp_metadata = {
    {name = 'id',    type = 'unsigned'},
    {name = 'iid',   type = 'unsigned'},
    {name = 'name',  type = 'string'},
    {name = 'type',  type = 'string'},
    {name = 'opts',  type = 'map'},
    {name = 'parts', type = 'array'},
}

for _ = 1, fiber_count do
    fiber.create(select_from_vindex)
end
for i = 1, fiber_count do
    local res = results:get()
    test:test(('result %d'):format(i), function(test)
        test:plan(2)
        test:is_deeply(res.metadata, exp_metadata, 'verify metadata')
        local rows_is_ok = type(res.rows) == 'table' and #res.rows > 0
        test:ok(rows_is_ok, 'verify rows')
    end)
end

os.exit(test:check() and 0 or 1)
