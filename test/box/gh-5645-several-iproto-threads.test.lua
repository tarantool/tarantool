env = require('test_run')
net_box = require('net.box')
fiber = require('fiber')
test_run = env.new()
test_run:cmd("create server test with script='box/gh-5645-several-iproto-threads.lua'")

test_run:cmd("setopt delimiter ';'")
function iproto_call(server_addr, fibers_count)
    local fibers = {}
    for i = 1, fibers_count do
        fibers[i] = fiber.new(function()
            local conn = net_box.new(server_addr)
            for _ = 1, 100 do
                pcall(conn.call, conn, 'ping')
            end
            conn:close()
        end)
        fibers[i]:set_joinable(true)
    end
    for _, f in ipairs(fibers) do
        f:join()
    end
end;
function get_network_stat()
    local total_net_stat_table = test_run:cmd(string.format("eval test 'return box.stat.net()'"))[1]
    assert(total_net_stat_table ~= nil)
    local connections = 0
    local requests = 0
    local sent = 0
    local received = 0
    for name, net_stat_table in pairs(total_net_stat_table) do
        assert(net_stat_table ~= nil)
        if name == "CONNECTIONS" then
            for name, val in pairs(net_stat_table) do
                if name == "total" then
                    connections = val
                end
            end
        elseif name == "REQUESTS" then
           for name, val in pairs(net_stat_table) do
                if name == "total" then
                    requests = val
                end
            end
        elseif name == "SENT" then
            for name, val in pairs(net_stat_table) do
                if name == "total" then
                    sent = val
                end
            end
        elseif name == "RECEIVED" then
            for name, val in pairs(net_stat_table) do
                if name == "total" then
                    received = val
                end
            end
        else
            assert(false)
        end
    end
    return connections, requests, sent, received
end
test_run:cmd("setopt delimiter ''");

-- Check that we can't start server with iproto threads count <= 0 or > 1000
-- We can't pass '-1' or another negative number as argument, so we pass string
opts = {}
opts.filename = 'gh-5645-several-iproto-threads.log'
test_run:cmd("start server test with args='negative' with crash_expected=True")
assert(test_run:grep_log("test", "Incorrect value for option 'iproto_threads'", nil, opts) ~= nil)
test_run:cmd("start server test with args='0' with crash_expected=True")
assert(test_run:grep_log("test", "Incorrect value for option 'iproto_threads'", nil, opts) ~= nil)
test_run:cmd("start server test with args='1001' with crash_expected=True")
assert(test_run:grep_log("test", "Incorrect value for option 'iproto_threads'", nil, opts) ~= nil)

-- We check that statistics gathered per each thread in sum is equal to
-- statistics gathered from all threads.
thread_count = 2
fibers_count = 100
test_run:cmd(string.format("start server test with args=\"%s\"", thread_count))
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]
iproto_call(server_addr, fibers_count)
-- Total statistics from all threads.
conn_t, req_t, sent_t, rec_t = get_network_stat()
-- Statistics per thread.
conn, req, sent, rec = 0, 0, 0, 0
assert(conn_t == fibers_count)

test_run:cmd("setopt delimiter ';'")
for thread_id = 1, thread_count do
    test_run:eval("test", string.format("errinj_set(%d)", thread_id - 1))
    local conn_c, req_c, sent_c, rec_c = get_network_stat()
    conn = conn + conn_c
    req = req + req_c
    sent = sent + sent_c
    rec = rec + rec_c
end;
test_run:cmd("setopt delimiter ''");
assert(conn_t == conn)
assert(req_t == req)
assert(sent_t == sent)
assert(rec_t == rec)

test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
