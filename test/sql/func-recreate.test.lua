test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

-- Check errors during function create process
fiber = require('fiber')
test_run:cmd("setopt delimiter ';'")
box.schema.func.create('WAITFOR', {language = 'Lua',
                       body = 'function (n) fiber.sleep(n) return n end',
                       param_list = {'integer'}, returns = 'integer',
                       exports = {'LUA', 'SQL'}})
test_run:cmd("setopt delimiter ''");

ch = fiber.channel(1)

_ = fiber.create(function () ch:put(box.execute('select WAITFOR(0.2)')) end)
fiber.sleep(0.1)

box.func.WAITFOR:drop()

test_run:cmd("setopt delimiter ';'")
box.schema.func.create('WAITFOR', {language = 'Lua',
                       body = 'function (n) fiber.sleep(n) return n end',
                       param_list = {'integer'}, returns = 'integer',
                       exports = {'LUA', 'SQL'}})
test_run:cmd("setopt delimiter ''");
ch:get()
box.func.WAITFOR:drop()

test_run:cmd("setopt delimiter ';'")
box.schema.func.create('WAITFOR', {language = 'Lua',
                   body = 'function (n) fiber.sleep(n) return n end',
                   param_list = {'integer'}, returns = 'integer',
                   exports = {'LUA', 'SQL'}})
test_run:cmd("setopt delimiter ''");

box.func.WAITFOR:drop()
