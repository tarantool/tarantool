env = require('test_run')
fiber = require('fiber')
test_run = env.new()

objcount = 10000
test_run:cmd("setopt delimiter ';'")
function test(op, count, space)
    if op == 1 then
        for key = 1, count do
            space:insert({key, key + 1000})
        end
    elseif op == 2 then
        for key = 1, count do
            space:replace({key, key + 5000})
        end
    elseif op == 3 then
        for key = 1, count do
            space:upsert({key, key + 5000}, {{'=', 2, key + 10000}})
        end
    elseif op == 4 then
        for key = 1, count do
            space:delete({key})
        end
    else
        assert(0)
    end
end;
test_run:cmd("setopt delimiter ''");

space = box.schema.space.create('test')
space:format({ {name = 'id', type = 'unsigned'}, \
               {name = 'year', type = 'unsigned'} })
_ = space:create_index('primary', { parts = {'id'} })
channel = fiber.channel(1)

-- Try to insert/replace/upsert/delete tuples in fiber
-- in parallel with the snapshot creation.
test_run:cmd("setopt delimiter ';'")
for op = 1, 4 do
    _ = fiber.create(function()
                         test(op, objcount, space)
                         channel:put(true)
                     end)
    box.snapshot()
    assert(channel:get() == true)
end;
test_run:cmd("setopt delimiter ''");

space:drop()
