env = require('test_run')
test_run = env.new()

fiber = require'fiber'

-- simple test for parallel ddl execution, gh-2783
_ = box.schema.space.create('test'):create_index('pk')

ch = fiber.channel(2)

test_run:cmd("setopt delimiter ';'")

function f1()
    local ok, err = pcall(function()
        box.space.test:create_index('sec', {parts = {2, 'num'}})
    end)
    ch:put(ok or err)
end;

function f2()
    local ok, err = pcall(function()
        box.space.test:create_index('third', {parts = {3, 'string'}})
    end)
    ch:put(ok or err)
end;

test_run:cmd("setopt delimiter ''");

_ = {fiber.create(f1), fiber.create(f2)}

ch:get()
ch:get()

_ = box.space.test:drop()
