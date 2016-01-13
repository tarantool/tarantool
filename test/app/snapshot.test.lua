#!/usr/bin/env tarantool
math = require('math')
fiber = require('fiber')

--
-- Check that Tarantool creates ADMIN session for #! script
--
continue_snapshoting = true

function noise()
    fiber.name('noise-'..fiber.id())
    while continue_snapshoting do
        if box.space.test:len() < 300000 then
            local  value = string.rep('a', math.random(255)+1)
            box.space.test:auto_increment{fiber.time64(), value}
        end
        fiber.sleep(0)
    end
end

function purge()
    fiber.name('purge-'..fiber.id())
    while continue_snapshoting do
        local min = box.space.test.index.primary:min()
        if min ~= nil then
            box.space.test:delete{min[1]}
        end
        fiber.sleep(0)
    end
end

function snapshot(lsn)
    fiber.name('snapshot')
    while continue_snapshoting do
        local new_lsn = box.info.server.lsn
        if new_lsn ~= lsn then
            lsn = new_lsn;
            pcall(box.snapshot)
        end
        fiber.sleep(0.001)
    end
    snap_chan:put("!")
end
box.cfg{logger="tarantool.log", slab_alloc_arena=0.1, rows_per_wal=5000}

if box.space.test == nil then
    box.schema.space.create('test')
    box.space.test:create_index('primary')
end

-- require('console').listen(3303)

fiber.create(noise)
fiber.create(purge)
fiber.create(noise)
fiber.create(purge)
fiber.create(noise)
fiber.create(purge)
fiber.create(noise)
fiber.create(purge)
snap_chan = fiber.channel()
snap_fib = fiber.create(snapshot, box.info.server.lsn)

fiber.sleep(0.3)
continue_snapshoting = false
snap_chan:get()
print('ok')

--https://github.com/tarantool/tarantool/issues/1185

s1 = box.schema.create_space('test1', { engine = 'memtx'})
i1 = s1:create_index('test', { type = 'tree', parts = {1, 'num'} })

s2 = box.schema.create_space('test2', { engine = 'memtx'})
i2 = s2:create_index('test', { type = 'tree', parts = {1, 'num'} })

for i = 1,1000 do s1:insert{i, i, i} end

fiber.create(function () box.snapshot() end)

fiber.sleep(0)

s2:insert{1, 2, 3}
s2:update({1}, {{'+', 2, 2}})

s1:drop()
s2:drop()

print('gh-1185 test done w/o crash!.')

os.exit(0)
