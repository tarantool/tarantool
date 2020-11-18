env = require('test_run')
test_run = env.new()

test_run:cmd("create server wal_async with script='box/wal_async.lua'")
test_run:cmd("start server wal_async")
test_run:cmd("switch wal_async")

box.cfg.wal_mode

-- Check usual path

s = box.schema.space.create('s')
i = s:create_index('pk')
s:insert{1}
s:insert{2}
s:insert{3}
s:select{}

-- Check that write is asynchronous

errinj = box.error.injection
errinj.set("ERRINJ_WAL_DELAY", true)

s:insert{4}
s:insert{5}
s:select{}

test_run:cmd("setopt delimiter ';'")
box:begin()
s:insert{6}
box:commit();
test_run:cmd("setopt delimiter ''");

errinj.set("ERRINJ_WAL_DELAY", false)

-- Check that data persisted in wal

test_run:cmd('restart server wal_async')

fiber = require('fiber')
test_run:cmd("setopt delimiter ';'")
rows = 0;
while true do
    s = box.space.s
    if s ~= nil then
        res = s:select{}
        rows = #res
    end
    if rows == 6 then
        break
    end
    fiber.sleep(0.01)
end;
test_run:cmd("setopt delimiter ''");

test_run:cmd("switch default")
test_run:cmd("stop server wal_async")
test_run:cmd("cleanup server wal_async")
