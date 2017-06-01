--
-- gh-1681: vinyl: crash in vy_rollback on ER_WAL_WRITE
--
test_run = require('test_run').new()
fiber = require('fiber')
errinj = box.error.injection
errinj.set("ERRINJ_VINYL_SCHED_TIMEOUT", 40)
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
function f() box.begin() s:insert{1, 'hi'} s:insert{2, 'bye'} box.commit() end
errinj.set("ERRINJ_WAL_WRITE", true)
f()
s:select{}
errinj.set("ERRINJ_WAL_WRITE", false)
f()
s:select{}
s:drop()
--
-- Lost data in case of dump error
--
--
test_run:cmd("setopt delimiter ';'")
if  box.cfg.vinyl_page_size > 1024 or box.cfg.vinyl_range_size > 65536 then
    error("This test relies on splits and dumps")
end;
s = box.schema.space.create('test', {engine='vinyl'});
_ = s:create_index('pk');
value = string.rep('a', 1024)
last_id = 1
-- fill up a range
function range()
    local range_size = box.cfg.vinyl_range_size
    local page_size = box.cfg.vinyl_page_size
    local s = box.space.test
    local num_rows = 0
    for i=1,range_size/page_size do
        for j=1, page_size/#value do
            s:replace({last_id, value})
            last_id = last_id + 1
            num_rows = num_rows + 1
        end
    end
    return num_rows
end;
num_rows = 0;
num_rows = num_rows + range();
box.snapshot();
errinj.set("ERRINJ_VY_RANGE_DUMP", true);
num_rows = num_rows + range();
-- fails due to error injection
box.snapshot();
errinj.set("ERRINJ_VY_RANGE_DUMP", false);
-- fails due to scheduler timeout
box.snapshot();
fiber.sleep(0.06);
num_rows = num_rows + range();
box.snapshot();
num_rows = num_rows + range();
box.snapshot();
num_rows;
for i=1,num_rows do
    if s:get{i} == nil then
        error("Row "..i.."not found")
    end
end;
#s:select{} == num_rows;
s:drop();
test_run:cmd("setopt delimiter ''");

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
for i = 1, 10 do s:insert({i, 'test str' .. tostring(i)}) end
box.snapshot()
s:select()
errinj.set("ERRINJ_VY_READ_PAGE", true)
s:select()
errinj.set("ERRINJ_VY_READ_PAGE", false)
s:select()

errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", true)
function test_cancel_read () k = s:select() return #k end
f1 = fiber.create(test_cancel_read)
fiber.cancel(f1)
-- task should be done
fiber.sleep(0.1)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", false);
s:select()

-- error after timeout for canceled fiber
errinj.set("ERRINJ_VY_READ_PAGE", true)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", true)
f1 = fiber.create(test_cancel_read)
fiber.cancel(f1)
fiber.sleep(0.1)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", false);
errinj.set("ERRINJ_VY_READ_PAGE", false);
s:select()
s:drop()

s = box.schema.space.create('test', {engine='vinyl'});
_ = s:create_index('pk');
_ = s:replace({1, string.rep('a', 128000)})
errinj.set("ERRINJ_WAL_WRITE_DISK", true)
box.snapshot()
errinj.set("ERRINJ_WAL_WRITE_DISK", false)
fiber.sleep(0.06)
_ = s:replace({2, string.rep('b', 128000)})
box.snapshot();
#s:select({1})
s:drop()

errinj.set("ERRINJ_VINYL_SCHED_TIMEOUT", 0)

--
-- Check that upsert squash fiber does not crash if index or
-- in-memory tree is gone.
--
errinj.set("ERRINJ_VY_SQUASH_TIMEOUT", 50)
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
s:insert{0, 0}
box.snapshot()
for i=1,256 do s:upsert({0, 0}, {{'+', 2, 1}}) end
box.snapshot() -- in-memory tree is gone
fiber.sleep(0.05)
s:select()
s:replace{0, 0}
box.snapshot()
for i=1,256 do s:upsert({0, 0}, {{'+', 2, 1}}) end
s:drop() -- index is gone
fiber.sleep(0.05)
errinj.set("ERRINJ_VY_SQUASH_TIMEOUT", 0)
