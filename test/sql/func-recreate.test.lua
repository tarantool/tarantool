test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

-- Check errors during function create process
fiber = require('fiber')
flag = true
box.internal.sql_create_function('WAITFOR', 'INT', function () while flag do fiber.sleep(0.01) end return 0 end)

ch = fiber.channel(1)

_ = fiber.create(function () ch:put(box.execute('select WAITFOR()')) end)

box.internal.sql_create_function('WAITFOR', 'INT', function () while flag do fiber.sleep(0.01) end return 0 end)
flag = false
ch:get()
box.internal.sql_create_function('WAITFOR', 'INT', function () while flag do fiber.sleep(0.01) end return 0 end)

