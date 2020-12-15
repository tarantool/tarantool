#!/usr/bin/env tarantool

local math = require('math')
local fiber = require('fiber')
local tap = require('tap')
local fio = require('fio')

box.cfg{ log="tarantool.log", memtx_memory=107374182}

local test = tap.test("snapshot")
test:plan(5)

-------------------------------------------------------------------------------
-- gh-695: Avoid overwriting tuple data with information necessary for smfree()
-------------------------------------------------------------------------------

local continue_snapshoting = true
local snap_chan = fiber.channel()

local function noise()
    fiber.name('noise-'..fiber.id())
    while continue_snapshoting do
        if box.space.test:len() < 300000 then
            local  value = string.rep('a', math.random(255)+1)
            box.space.test:auto_increment{fiber.time64(), value}
        end
        fiber.sleep(0)
    end
end

local function purge()
    fiber.name('purge-'..fiber.id())
    while continue_snapshoting do
        local min = box.space.test.index.primary:min()
        if min ~= nil then
            box.space.test:delete{min[1]}
        end
        fiber.sleep(0)
    end
end

local function snapshot(lsn)
    fiber.name('snapshot')
    while continue_snapshoting do
        local new_lsn = box.info.lsn
        if new_lsn ~= lsn then
            lsn = new_lsn;
            pcall(box.snapshot)
        end
        fiber.sleep(0.001)
    end
    snap_chan:put("!")
end

box.once("snapshot.test", function()
    box.schema.space.create('test')
    box.space.test:create_index('primary')
end)

fiber.create(noise)
fiber.create(purge)
fiber.create(noise)
fiber.create(purge)
fiber.create(noise)
fiber.create(purge)
fiber.create(noise)
fiber.create(purge)
fiber.create(snapshot, box.info.lsn)

fiber.sleep(0.3)
continue_snapshoting = false
snap_chan:get()

test:ok(true, 'gh-695: avoid overwriting tuple data necessary for smfree()')

-------------------------------------------------------------------------------
-- gh-1185: Crash in matras_touch in snapshot_daemon.test
-------------------------------------------------------------------------------

local s1 = box.schema.create_space('test1', { engine = 'memtx'})
s1:create_index('test', { type = 'tree', parts = {1, 'unsigned'} })

local s2 = box.schema.create_space('test2', { engine = 'memtx'})
s2:create_index('test', { type = 'tree', parts = {1, 'unsigned'} })

for i = 1,1000 do s1:insert{i, i, i} end

local snap_chan = fiber.channel()
fiber.create(function () box.snapshot() snap_chan:put(true) end)

fiber.sleep(0)

s2:insert{1, 2, 3}
s2:update({1}, {{'+', 2, 2}})

s1:drop()
s2:drop()

snap_chan:get()

test:ok(true, "gh-1185: no crash in matras_touch")

-------------------------------------------------------------------------------
-- gh-1084: box.snapshot() aborts if the server is out of file descriptors
-------------------------------------------------------------------------------

local function gh1094()
    local msg = "gh-1094: box.snapshot() doesn't abort if out of file descriptors"
    local nfile
    local ulimit = io.popen('ulimit -n')
    if ulimit then
        nfile = tonumber(ulimit:read())
        ulimit:close()
    end

    if not nfile or nfile > 1024 then
        -- descriptors limit is to high, just skip test
        test:ok(true, msg)
        return
    end
    local files = {}
    for i = 1,nfile do
        files[i] = fio.open('/dev/null')
        if files[i] == nil then
            break
        end
    end
    local sf = pcall(box.snapshot)
    for _, f in pairs(files) do
        f:close()
    end
    local ss = pcall(box.snapshot)
    test:ok(not sf and ss, msg)
end
gh1094()

-- gh-2045 - test snapshot if nothing changed
-- we wan't check snapshot update time because it may take long time to wait
box.snapshot()
box.snapshot()
box.snapshot()
test:ok(true, 'No crash for second snapshot w/o any changes')
local files = fio.glob(box.cfg.memtx_dir .. '/*.snap')
table.sort(files)
fio.unlink(files[#files])
box.snapshot()
test:ok(fio.stat(files[#files]) ~= nil, "Snapshot was recreated")

box.space.test:drop()

test:check()
os.exit(0)
