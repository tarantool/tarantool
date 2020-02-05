env = require('test_run')
test_run = env.new()

fiber = require'fiber'

-- simple test for parallel ddl execution
_ = box.schema.space.create('test'):create_index('pk')

ch = fiber.channel(2)

test_run:cmd("setopt delimiter ';'")

function f1()
    box.space.test:create_index('sec', {parts = {2, 'num'}})
    ch:put(true)
end;

function f2()
    box.space.test:create_index('third', {parts = {3, 'string'}})
    ch:put(true)
end;

test_run:cmd("setopt delimiter ''");

_ = {fiber.create(f1), fiber.create(f2)}

ch:get()
ch:get()

_ = box.space.test:drop()
