--
-- Check that ranges left from old dumps and incomplete splits
-- are ignored during initial recovery
-- The idea behind the test is simple - create several invalid range files,
-- i.e. those left from previous dumps and incomplete splits, then restart
-- the server and check that the content of the space was not corrupted.
--
-- To make it possible, we need to (1) prevent the garbage collector from
-- removing unused range files and (2) make the split procedure fail after
-- successfully writing the first range. We use error injection to achieve
-- that.
--
-- The test runs as follows:
--
--  1. Disable garbage collection with the aid of error injection.
--
--  2. Add a number of tuples to the test space that would make it split.
--     Rewrite them several times with different values so that different
--     generations of ranges on disk would have different contents.
--
--  3. Inject error to the split procedure.
--
--  4. Rewrite the tuples another couple of rounds. This should trigger
--     split which is going to fail leaving invalid range files with newer
--     ids on the disk.
--
--  5. Restart the server and check that the test space content was not
--     corrupted.
--
--
test_run = require('test_run').new()
errinj = box.error.injection

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('primary')

test_run:cmd("setopt delimiter ';'")
function gen(i)
    local pad_size = 256
    local range_count = 5
    local pad = string.rep('x', pad_size + i)
    local n = (range_count + i) * math.floor(box.cfg.vinyl_range_size / pad_size)
    for k = 1,n do
        s:replace{k, i + k, pad}
    end
    pcall(box.snapshot)
end;
test_run:cmd("setopt delimiter ''");

errinj.set("ERRINJ_VY_GC", true)
for i=1,4 do gen(i) end
errinj.set("ERRINJ_VY_RANGE_SPLIT", true)
for i=5,6 do gen(i) end

-- Check that the total size of data stored on disk, the number of pages,
-- and the number of runs are the same before and after recovery.
tmp = box.schema.space.create('tmp')
_ = tmp:create_index('primary')
_ = tmp:insert{0, s.index.primary:info()}

test_run:cmd('restart server default')

s = box.space.test
tmp = box.space.tmp

vyinfo1 = tmp:select(0)[1][2]
vyinfo2 = s.index.primary:info()
vyinfo1.size == vyinfo2.size or {vyinfo1.size, vyinfo2.size}
vyinfo1.page_count == vyinfo2.page_count or {vyinfo1.page_count, vyinfo2.page_count}
vyinfo1.run_count == vyinfo2.run_count or {vyinfo1.run_count, vyinfo2.run_count}

tmp:drop()

test_run:cmd("setopt delimiter ';'")
function check(i)
    local pad_size = 256
    local range_count = 5
    local n = (range_count + i) * math.floor(box.cfg.vinyl_range_size / pad_size)
    local n_corrupted = 0
    for k=1,n do
        local v = s:get(k)
        if not v or v[2] ~= i + k then
			n_corrupted = n_corrupted + 1
		end
    end
    return n - s:count(), n_corrupted
end;
test_run:cmd("setopt delimiter ''");

check(6)

s:drop()
