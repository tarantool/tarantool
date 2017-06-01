env = require('test_run')
test_run = env.new()
test_run:cmd("restart server default")
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
        tester:insert{#n, n}
    end
end;
test_run:cmd("setopt delimiter ''");
tuple_max()
tuple_max = string.rep('a', 1000000)
#tuple_max
t = tester:insert{#tuple_max, tuple_max}
tester:drop()
-- need to restart because we need to have a clean server after
-- related to #2280
test_run:cmd("restart server default")
test_run:cmd("restart server default")
