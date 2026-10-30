-- This test checks streams iplementation in iproto (gh-5860).
net_box = require('net.box')
fiber = require('fiber')
test_run = require('test_run').new()

test_run:cmd("create server test with script='box/iproto_streams.lua'")

test_run:cmd("setopt delimiter ';'")
function get_current_connection_count()
    local total_net_stat_table =
        test_run:cmd(string.format("eval test 'return box.stat.net()'"))[1]
    assert(total_net_stat_table)
    local connection_stat_table = total_net_stat_table.CONNECTIONS
    assert(connection_stat_table)
    return connection_stat_table.current
end;
function get_current_stream_count()
    local total_net_stat_table =
        test_run:cmd(string.format("eval test 'return box.stat.net()'"))[1]
    assert(total_net_stat_table)
    local stream_stat_table = total_net_stat_table.STREAMS
    assert(stream_stat_table)
    return stream_stat_table.current
end;
function get_current_msg_count()
    local total_net_stat_table =
        test_run:cmd(string.format("eval test 'return box.stat.net()'"))[1]
    assert(total_net_stat_table)
    local request_stat_table = total_net_stat_table.REQUESTS
    assert(request_stat_table)
    return request_stat_table.current
end;
function wait_and_return_results(futures)
    local results = {}
    for name, future in pairs(futures) do
        local err
        results[name], err = future:wait_result()
        if err then
            results[name] = err
        end
    end
    return results
end;
test_run:cmd("setopt delimiter ''");

-- Some simple checks for new object - stream
test_run:cmd("start server test with args='1'")
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]
conn_1 = net_box.connect(server_addr)
stream_1 = conn_1:new_stream()
conn_2 = net_box.connect(server_addr)
stream_2 = conn_2:new_stream()
-- Stream is a wrapper around connection, so if you close connection
-- you close stream, and vice versa.
conn_1:close()
assert(not stream_1:ping())
stream_2:close()
assert(not conn_2:ping())
conn = net_box.connect(server_addr)
stream = conn:new_stream()
-- The new method `new_stream`, for the stream object, returns a new
-- stream object, just as in the case of connection.
_ = stream:new_stream()
conn:close()

-- Check that spaces in stream object updates, during reload_schema
conn = net_box.connect(server_addr)
stream = conn:new_stream()
test_run:switch("test")
-- Create one space on server
s = box.schema.space.create('test', { engine = 'memtx' })
_ = s:create_index('primary')
test_run:switch("default")
assert(not conn.space.test)
assert(not stream.space.test)
assert(conn.schema_version == stream._schema_version)
conn:reload_schema()
assert(conn.space.test ~= nil)
assert(conn.schema_version ~= stream._schema_version)
assert(stream.space.test ~= nil)
-- When we touch stream.space, we compare stream._schema_version
-- and conn.schema_version if they are not equal, we clear stream
-- space cache, update it's _schema_version and load space from
-- connection to stream space cache.
assert(conn.schema_version == stream._schema_version)
collectgarbage()
collectgarbage()
assert(conn.space.test ~= nil)
assert(stream.space.test ~= nil)
test_run:switch("test")
s:drop()
test_run:switch("default")
conn:reload_schema()
assert(not conn.space.test)
assert(not stream.space.test)
test_run:cmd("stop server test")

-- All test works with iproto_thread count = 10

test_run:cmd("start server test with args='10'")
test_run:switch('test')
fiber = require('fiber')
s = box.schema.space.create('test', { engine = 'memtx' })
_ = s:create_index('primary')
test_run:cmd("setopt delimiter ';'")
function replace_with_yeild(item)
    fiber.sleep(0.1)
    return s:replace({item})
end;
test_run:cmd("setopt delimiter ''");
test_run:switch('default')

conn = net_box.connect(server_addr)
assert(conn:ping())
conn_space = conn.space.test
stream = conn:new_stream()
stream_space = stream.space.test

-- Check that all requests in stream processed consistently
futures = {}
replace_count = 3
test_run:cmd("setopt delimiter ';'")
for i = 1, replace_count do
    futures[string.format("replace_%d", i)] =
        stream_space:replace({i}, {is_async = true})
    futures[string.format("select_%d", i)] =
        stream_space:select({}, {is_async = true})
end;
futures["replace_with_yeild_for_stream"] =
    stream:call("replace_with_yeild",
                { replace_count + 1 }, {is_async = true});
futures["select_with_yeild_for_stream"] =
    stream_space:select({}, {is_async = true});
test_run:cmd("setopt delimiter ''");
results = wait_and_return_results(futures)
-- [1]
assert(results["select_1"])
-- [1] [2]
assert(results["select_2"])
-- [1] [2] [3]
assert(results["select_3"])
-- [1] [2] [3] [4]
-- Even yeild in replace function does not affect
-- the order of requests execution in stream
assert(results["select_with_yeild_for_stream"])

-- There is no request execution order for the connection
futures = {}
test_run:cmd("setopt delimiter ';'")
futures["replace_with_yeild_for_connection"] =
    conn:call("replace_with_yeild", { replace_count + 2 }, {is_async = true});
futures["select_with_yeild_for_connection"] =
    conn_space:select({}, {is_async = true});
test_run:cmd("setopt delimiter ''");
results = wait_and_return_results(futures)
-- [1] [2] [3] [4]
-- Select will be processed earlier because of
-- yeild in `replace_with_yeild` function
assert(results["select_with_yeild_for_connection"])
test_run:wait_cond(function () return get_current_stream_count() == 0 end)
test_run:wait_cond(function () return get_current_msg_count() == 0 end)
test_run:switch("test")
-- [1] [2] [3] [4] [5]
s:select()
test_run:switch('default')
conn:close()
test_run:wait_cond(function () return get_current_connection_count() == 0 end)

-- Check that all request will be processed
-- after connection close.
conn = net_box.connect(server_addr)
stream = conn:new_stream()
space = stream.space.test
test_run:cmd("setopt delimiter ';'")
replace_count = 20
for i = 1, replace_count do
    space:replace({i}, {is_async = true})
end;
test_run:cmd("setopt delimiter ''");
-- Give time to send
fiber.sleep(0)
conn:close()
test_run:wait_cond(function () return get_current_stream_count() == 0 end)
test_run:wait_cond(function () return get_current_msg_count() == 0 end)
test_run:wait_cond(function () return get_current_connection_count() == 0 end)
test_run:switch("test")
-- select return tuples from [1] to [20]
-- because all messages processed after
-- connection closed
s:select{}
s:drop()
test_run:switch("default")
test_run:cmd("stop server test")

test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
