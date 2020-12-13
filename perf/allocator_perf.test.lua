#!/usr/bin/env ../src/tarantool
os.execute('rm -rf *.snap *.xlog *.vylog ./512 ./513 ./514 ./515 ./516 ./517 ./518 ./519 ./520 ./521')
local clock = require('clock')
box.cfg{listen = 3301, wal_mode='none', allocator=arg[1]}
local space = box.schema.space.create('test')
space:format({ {name = 'id', type = 'unsigned'}, {name = 'year', type = 'unsigned'} })
space:create_index('primary', { parts = {'id'} })
local time_insert = 0
local time_replace = 0
local time_delete = 0
local cnt = 0
local cnt_max = 20
local op_max = 2500000
local nanosec = 1.0e9
while cnt < cnt_max do
    cnt = cnt + 1
    local time_before = clock.monotonic64()
    for key = 1, op_max do space:insert({key, key + 1000}) end
    local time_after = clock.monotonic64()
    time_insert = time_insert + (time_after - time_before)
    time_before = clock.monotonic64()
    for key = 1, op_max do space:replace({key, key + 5000}) end
    time_after = clock.monotonic64()
    time_replace = time_replace + (time_after - time_before)
    time_before = clock.monotonic64()
    for key = 1, op_max do space:delete(key) end
    time_after = clock.monotonic64()
    time_delete = time_delete + (time_after - time_before)
end
io.write("{\n")
io.write(string.format("  \"alloc time\": \"%.3f\"\n", tonumber(time_insert) / (nanosec * cnt_max)))
io.write(string.format("  \"replace time\": \"%.3f\"\n", tonumber(time_replace) / (nanosec * cnt_max)))
io.write(string.format("  \"delete time\": \"%.3f\"\n}\n", tonumber(time_delete) / (nanosec * cnt_max)))
os.exit()
