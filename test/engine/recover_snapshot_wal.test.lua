env = require('test_run')
test_run = env.new()
-- write data recover from latest snapshot and logs

test_run:cmd("restart server default")

engine = test_run:get_cfg('engine')
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')

space:insert({0})
box.snapshot()
space:insert({33001})

test_run:cmd("restart server default")

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})

for key = 1, 351 do space:insert({key}) end
box.snapshot()
-- Insert so many tuples, that recovery would need to yield
-- periodically to allow other fibers do something. At the moment
-- of writing this the yield period was 32k tuples.
box.begin()                                                     \
for key = 352, 33000 do                                         \
    space:insert({key})                                         \
    if key % 10000 == 0 then                                    \
        box.commit()                                            \
        box.begin()                                             \
    end                                                         \
end                                                             \
box.commit()

test_run:cmd("restart server default")

space = box.space['test']
index = space.index['primary']
i = 0
err = nil
for _, t in space:pairs() do                                    \
    if t[1] ~= i then                                           \
        err = {i, t}                                            \
        break                                                   \
    end                                                         \
    i = i + 1                                                   \
end
i
err

space:drop()
test_run:cmd("restart server default with cleanup=1")
