--
-- gh-1681: vinyl: crash in vy_rollback on ER_WAL_WRITE
--
test_run = require('test_run').new()
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
-- errinj.set("ERRINJ_VY_RANGE_CREATE", true);
num_rows = num_rows + range();
box.snapshot();
-- errinj.set("ERRINJ_VY_RANGE_CREATE", false);
num_rows = num_rows + range();
box.snapshot();
num_rows;
for i=1,num_rows do
    if s:get{i} == nil then
        error("Row "..i.."not found")
    end
end;
s:drop();
test_run:cmd("setopt delimiter ''")
