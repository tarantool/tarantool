env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default with cleanup=1')
-- 
-- Test various tuple bugs which do not require a write ahead log.
-- 
-- -------------------------------------------------------
-- gh-372 Assertion with a function that inserts increasingly
-- large tables
-- -------------------------------------------------------

tester = box.schema.space.create('tester')
index = tester:create_index('primary',{})
test_run:cmd("setopt delimiter ';'")
function tuple_max()
    local n = 'a'
    while true do
        n = n..n
        local status, reason = pcall(tester.insert, tester, {#n, n})
        if not status then
            return #n, reason
        end
        collectgarbage('collect')
    end
end;
test_run:cmd("setopt delimiter ''");
n, reason = tuple_max()
n
n + 32 >= box.cfg.memtx_max_tuple_size
reason
tester:drop()
tuple_max = nil
collectgarbage('collect')
