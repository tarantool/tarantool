--
-- gh-1681: vinyl: crash in vy_rollback on ER_WAL_WRITE
--
test_run = require('test_run').new()
fiber = require('fiber')
errinj = box.error.injection
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
if  box.cfg.vinyl.page_size > 1024 or box.cfg.vinyl.range_size > 65536 then
    error("This test relies on splits and dumps")
end;
s = box.schema.space.create('test', {engine='vinyl'});
_ = s:create_index('pk');
-- fill up a range
function range()
    local range_size = box.cfg.vinyl.range_size
    local page_size = box.cfg.vinyl.page_size
    local s = box.space.test
    local num_rows = 0
    for i=1,range_size/page_size do
        local value = string.rep('a', 256)
        for j=1, page_size/#value do
            s:auto_increment{value}
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
fiber.sleep(1);
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
fiber.sleep(0.2)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", false);
s:select()

-- error after timeout for canceled fiber
errinj.set("ERRINJ_VY_READ_PAGE", true)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", true)
f1 = fiber.create(test_cancel_read)
fiber.cancel(f1)
fiber.sleep(0.2)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", false);
errinj.set("ERRINJ_VY_READ_PAGE", false);
s:select()
s:drop()

