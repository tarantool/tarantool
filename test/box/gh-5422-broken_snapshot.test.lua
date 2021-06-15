env = require('test_run')
fio = require('fio')
test_run = env.new()

test_run:cmd("setopt delimiter ';'")
function get_snapshot_name ()
    local snapshot = nil
    local directory = fio.pathjoin(fio.cwd(), 'gh-5422-broken_snapshot')
    for files in io.popen(string.format("ls %s", directory)):lines() do
        local snapshots = string.find(files, "snap")
        if (snapshots ~= nil) then
            snapshot = string.find(files, "%n")
            if (snapshot ~= nil) then
                snapshot = string.format("%s/%s", directory, files)
            end
        end
    end
    return snapshot
end;
function get_file_size(filename)
    local file = io.open(filename, "r")
    local size = file:seek("end")
    io.close(file)
    return size
end;
function write_garbage_with_restore_or_save(filename, offset, count, restore)
    if restore == true then
        os.execute(string.format('cp %s.save %s', snapshot, snapshot))
    else
        os.execute(string.format('cp %s %s.save', filename, filename))
    end
    local file = io.open(filename, "r+b")
    file:seek("set", offset)
    for i = 1, count do
        file:write(math.random(1,254))
    end
    io.close(file)
end;
function check_count_valid_snapshot_data(count)
    local cnt = 0
    local val = test_run:eval('test', "box.space.test:select()")[1]
    for i = 1, count do
        if val[i] ~= nil then
            cnt = cnt + 1
        end
    end
    return cnt
end;
test_run:cmd("setopt delimiter ''");

garbage_size = 500
corruption_offset = 15000

test_run:cmd("create server test with script='box/gh-5422-broken_snapshot.lua'")
test_run:cmd("start server test")
test_run:cmd("switch test")
items_count = 20000
-- Create space and snapshot file
space = box.schema.space.create('test', { engine = "memtx" })
space:format({ {name = 'id', type = 'unsigned'} })
index = space:create_index('primary', { parts = {'id'} })
for key = 1, items_count do space:insert({key}) end
box.snapshot()

test_run:cmd("switch default")
items_count = test_run:eval("test", "items_count")[1]
snapshot = get_snapshot_name()
size = get_file_size(snapshot)

-- Write data at the end of the file
write_garbage_with_restore_or_save(snapshot, size, garbage_size, false)
test_run:cmd("restart server test with script='box/gh-5422-broken_snapshot.lua'")
test_run:cmd("switch default")
-- Check that all data valid
assert(check_count_valid_snapshot_data(items_count) == items_count)

-- Restore snapshot
os.execute(string.format('cp %s.save %s', snapshot, snapshot))
-- truncate
os.execute(string.format('dd if=%s.save of=%s bs=%d count=1', snapshot, snapshot, size - corruption_offset))
test_run:cmd("restart server test with script='box/gh-5422-broken_snapshot.lua'")
test_run:cmd("switch default")
-- Check that some data valid
valid_data_count_1 = check_count_valid_snapshot_data(items_count)
assert(valid_data_count_1 > 0)

-- Restore snapshot and write garbage at the middle of file
write_garbage_with_restore_or_save(snapshot, size - corruption_offset, garbage_size, true)
test_run:cmd("restart server test with script='box/gh-5422-broken_snapshot.lua'")
test_run:cmd("switch default")
-- Check that some data valid.
-- Count of valid data is greater than we truncate snapshot.
valid_data_count_2 = check_count_valid_snapshot_data(items_count)
assert(valid_data_count_2 > 0)

-- Restore snapshot and write big garbage at the start of the file
write_garbage_with_restore_or_save(snapshot, 5000, garbage_size, true)
test_run:cmd("stop server test")
-- Check that we unable to start with corrupted system space
test_run:cmd("start server test with crash_expected=True")
opts = {}
opts.filename = 'gh-5422-broken_snapshot.log'
-- We must not find ER_UNKNOWN_REPLICA in log file
assert(test_run:grep_log("test", "ER_UNKNOWN_REPLICA", nil, opts) == nil)

-- Check that snapshot filename in log file is not corrupted
os.execute(string.format('cp %s.save %s', snapshot, snapshot))
os.execute(string.format('dd if=%s.save of=%s bs=%d count=1', snapshot, snapshot, size - corruption_offset))
test_run:cmd("start server test")
test_run:cmd("stop server test")
os.remove(string.format('%s.save', snapshot))
assert(test_run:grep_log("test", ".snap' has no EOF marker", nil, opts))
test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
