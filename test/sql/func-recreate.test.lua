test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

-- Check errors during function create process
fiber = require('fiber')
box.internal.sql_create_function('WAITFOR', 'INT', function (n) fiber.sleep(n) return n end)

ch = fiber.channel(1)

_ = fiber.create(function () ch:put(box.execute('select WAITFOR(0.2)')) end)
fiber.sleep(0.1)

box.internal.sql_create_function('WAITFOR', 'INT', function (n) require('fiber').sleep(n) return n end)
ch:get()
box.internal.sql_create_function('WAITFOR', 'INT', function (n) require('fiber').sleep(n) return n end)

