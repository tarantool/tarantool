env = require('test_run')
test_run = env.new()

s = box.schema.space.create('test', {engine = 'memtx'})
i1 = s:create_index('pk')
i2 = s:create_index('mk', {parts={{'[2][*]', 'uint'}}})
test_run:cmd("setopt delimiter ';'")
function test()
    local t = {1, {}}
    for i = 1,65536 do
        table.insert(t[2], i)
        if (i % 4096 == 0) then
            s:replace(t)
        end
    end
end;
test_run:cmd("setopt delimiter ''");

pcall(test) -- must fail but not crash

test = nil
s:drop()