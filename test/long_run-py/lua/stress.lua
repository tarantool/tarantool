#!/usr/bin/env tarantool

fiber = require('fiber')
ffi = require('ffi')

box.cfg {
    listen            = os.getenv("LISTEN"),
    slab_alloc_arena  = 4,
    rows_per_wal      = 1000000,
    phia = {
        threads = 3;
        memory_limit = 0.05;
    }
}

require('console').listen(os.getenv('ADMIN'))

local pcall_lua = pcall

local function pcall_wrap(status, ...)
    if status ~= true then
        return false, tostring(...)
    end
    return status, ...
end


pcall_e = function(fun, ...)
    return pcall_wrap(pcall_lua(fun, ...))
end

s1 = box.schema.space.create('s1', { engine = 'phia', if_not_exists = true })
s1:create_index('pk', {if_not_exists = true})

s2 = box.schema.space.create('s2', { engine = 'phia', if_not_exists = true })
s2:create_index('pk', {compression = 'zstd', compression_branch = 'zstd', if_not_exists = true})

s3 = box.schema.space.create('s3', { engine = 'phia', if_not_exists = true })
s3:create_index('pk', {compression = 'zstd', compression_branch = 'zstd', amqf = 1, if_not_exists = true})

s4 = box.schema.space.create('s4', { engine = 'phia', if_not_exists = true })
s4:create_index('pk', {compression = 'lz4', compression_branch = 'lz4', amqf = 1, if_not_exists = true})

ch = fiber.channel(16)
cnt = 0

spaces = {s1, s2, s3, s4}

function t1()
    local t1 = fiber.time()
    while fiber.time() - t1 < 300 do
        local k = math.random(10000)
        local t = math.random(80)
        local data = string.char(math.random(string.byte('Z') - string.byte('A')) + string.byte('A') - 1)
        data = data:rep(math.random(20480))
        local space = spaces[math.mod(t, 4) + 1]
        if t < 32 then
            space:replace({k, data})
        elseif t < 40 then
            space:upsert({k, data}, {{'=', 2, data}})
        elseif t < 56 then
            pcall_e(space.insert, space, {k, data})
        elseif t < 64 then
            space:delete({k})
        else
            pcall_e(space.update, space, {k}, {{'=', 2, data}})
        end
    end
    ch:put(1)
end;

function t2()
    local t1 = fiber.time()
    while fiber.time() - t1 < 300 do
        local k = math.random(10000)
        local t = math.random(16)
        local space = spaces[math.mod(t, 4) + 1]
        if t < 12 then
            local l = space:get({k})
        else
            space:delete({k})
        end
    end
    ch:put(2)
end;

function t3()
    local t1 = fiber.time()
    while fiber.time() - t1 < 300 do
        local k = math.random(10000)
        local t = math.random(20)
        local l = math.random(2048)
        local space = spaces[math.mod(t, 4) + 1]
        if t <= 6 then
            local l = space:select(k, { iterator = 'GE', limit = l })
        elseif t <= 12 then
            local l = space:select(k, { iterator = 'LE', limit = l })
        else
            space:delete({k})
        end
    end
    ch:put(3)
end;

math.randomseed(os.time())

for i = 1, 6 do
    fiber.create(t1)
end;


for i = 1, 6 do
    fiber.create(t2)
end;

for i = 1, 4 do
    fiber.create(t3)
end;


for i = 1, 16 do
    print(ch:get())
end;

box.snapshot()
