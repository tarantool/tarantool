--# stop server default
--# start server default
-- 
-- Test various tuple bugs which do not require a write ahead log.
-- 
-- -------------------------------------------------------
-- gh-372 Assertion with a function that inserts increasingly
-- large tables
-- -------------------------------------------------------
tester = box.schema.create_space('tester')
tester:create_index('primary',{})
--# setopt delimiter ';'
function tuple_max()
    local n = 'a'
    while true do
        n = n..n
        tester:insert{#n, n}
    end
end;
--# setopt delimiter=''
tuple_max()
tuple_max = string.rep('a', 1000000)
#tuple_max
t = tester:insert{#tuple_max, tuple_max}
tester:drop()
