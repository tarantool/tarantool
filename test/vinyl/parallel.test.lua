env = require('test_run')
test_run = env.new()
fiber = require('fiber')

s = box.schema.space.create('vinyl', {engine = 'vinyl'})
i = s:create_index('primary', {type = 'tree'})
n = 10
m = 10
ch = fiber.channel(m)

test_run:cmd("setopt delimiter ';'")
function stest(l, m, n)
    for i = 0, n - 1 do
        s:insert({i * m + l, 'tuple ' .. tostring(i * m + l)})
    end
    ch:put(1)
end;
test_run:cmd("setopt delimiter ''");

for i = 0, m - 1 do f = fiber.create(stest, i, m, n) end

cnt = 0
start_time = fiber.time()
test_run:cmd("setopt delimiter ';'")
for i = 0, m - 1 do 
    tm = start_time + 2 - fiber.time()
    if tm < 0 then
      tm = 0
    end
    cnt = cnt + (ch:get(tm) or 0)
end;
test_run:cmd("setopt delimiter ''");

cnt == m
i:count() == m * n

s:drop()
