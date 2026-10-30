env = require('test_run')
test_run = env.new()

fiber = require'fiber'
ch = fiber.channel(3)

_ = box.schema.space.create('test'):create_index('pk')

test_run:cmd("setopt delimiter ';'")
function add_index()
    box.space.test:create_index('sec', {parts = {2, 'num'}})
    ch:put(true)
end;

function insert_tuple(tuple)
    ch:put({pcall(box.space.test.replace, box.space.test, tuple)})
end;
test_run:cmd("setopt delimiter ''");

_ = {fiber.create(insert_tuple, {1, 2, 'a'}), fiber.create(add_index), fiber.create(insert_tuple, {2, '3', 'b'})}
{ch:get(), ch:get(), ch:get()}

box.space.test:select()

test_run:cmd('restart server default')

box.space.test:select()
box.space.test:drop()
