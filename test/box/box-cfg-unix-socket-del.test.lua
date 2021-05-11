env = require('test_run')
fio = require('fio')
test_run = env.new()
test_run:cmd("create server test with script='box/box-cfg-unix-socket-del.lua'")

-- Check that unix socket path deleted after tarantool is finished
test_run:cmd("setopt delimiter ';'")
for i = 1, 2 do
    local thread_count = i
    test_run:cmd(string.format("start server test with args=\"%s\"", thread_count))
    server_addr = test_run:eval('test', 'return box.cfg.listen')[1]
    test_run:cmd("stop server test")
    assert(fio.path.exists(server_addr) == false)
end
test_run:cmd("setopt delimiter ''");

-- Check, that all sockets are closed correctly,
-- when the listening address is changed.
test_run:cmd("start server test with args=2")
server_addr_before = test_run:eval('test', 'return box.cfg.listen')[1]
test_run:eval('test', string.format("box.cfg{ listen = \'%s\' }", server_addr_before .. "X"))
server_addr_after = test_run:eval('test', 'return box.cfg.listen')[1]
assert(server_addr_after == server_addr_before .. "X")
assert(test_run:grep_log("test", "Bad file descriptor") == nil)
assert(test_run:grep_log("test", "No such file or directory") == nil)
test_run:eval('test', string.format("box.cfg { listen = \'%s\' }", server_addr_before))
server_addr_result = test_run:eval('test', 'return box.cfg.listen')[1]
assert(server_addr_result == server_addr_before)
test_run:cmd("stop server test")
assert(not fio.path.exists(server_addr_before))
assert(not fio.path.exists(server_addr_after))

test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
