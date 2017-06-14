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

test_run:cmd('restart server default')

env = require('test_run')
test_run = env.new()
fiber = require'fiber'

ch = fiber.channel(2)

--issue #928
space = box.schema.space.create('test_trunc')
_ = space:create_index('pk')
_ = box.space.test_trunc:create_index('i1', {type = 'hash', parts = {2, 'STR'}})
_ = box.space.test_trunc:create_index('i2', {type = 'hash', parts = {2, 'STR'}})

function test_trunc() space:truncate() ch:put(true) end

_ = {fiber.create(test_trunc), fiber.create(test_trunc)}
_ = {ch:get(), ch:get()}
space:drop()
