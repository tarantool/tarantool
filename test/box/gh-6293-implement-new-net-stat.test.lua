env = require('test_run')
net_box = require('net.box')
test_run = env.new()

test_run:cmd("create server test with script=\
              'box/gh-6293-implement-new-net-stat.lua'")

test_run:cmd("setopt delimiter ';'")
function get_network_requests_stats_using_call(rtype)
    local box_stat_net = test_run:cmd(string.format(
        "eval test 'return box.stat.net()'"
    ))[1]
    local total = box_stat_net.REQUESTS[rtype]
    local in_progress = box_stat_net.REQUESTS_IN_PROGRESS[rtype]
    local in_stream_queue = box_stat_net.REQUESTS_IN_STREAM_QUEUE[rtype]
    return total, in_progress, in_stream_queue
end;
function get_network_requests_stats_for_thread_using_call(thread_id, rtype)
    local box_stat_net = test_run:cmd(string.format(
        "eval test 'return box.stat.net.thread()'"
    ))[1][thread_id]
    local total = box_stat_net.REQUESTS[rtype]
    local in_progress = box_stat_net.REQUESTS_IN_PROGRESS[rtype]
    local in_stream_queue = box_stat_net.REQUESTS_IN_STREAM_QUEUE[rtype]
    return total, in_progress, in_stream_queue
end;
function get_network_requests_stats_using_index(rtype)
    local total = test_run:cmd(string.format(
        "eval test 'return box.stat.net.%s.%s'",
        "REQUESTS", rtype
    ))[1]
    local in_progress = test_run:cmd(string.format(
        "eval test 'return box.stat.net.%s.%s'",
        "REQUESTS_IN_PROGRESS", rtype
    ))[1]
    local in_stream_queue = test_run:cmd(string.format(
        "eval test 'return box.stat.net.%s.%s'",
        "REQUESTS_IN_STREAM_QUEUE", rtype
    ))[1]
    return total, in_progress, in_stream_queue
end;
function get_network_requests_stats_for_thread_using_index(thread_id, rtype)
    local total = test_run:cmd(string.format(
        "eval test 'return box.stat.net.thread[%d].%s.%s'",
        thread_id, "REQUESTS", rtype
    ))[1]
    local in_progress = test_run:cmd(string.format(
        "eval test 'return box.stat.net.thread[%d].%s.%s'",
        thread_id, "REQUESTS_IN_PROGRESS", rtype
    ))[1]
    local in_stream_queue = test_run:cmd(string.format(
        "eval test 'return box.stat.net.thread[%d].%s.%s'",
        thread_id, "REQUESTS_IN_STREAM_QUEUE", rtype
    ))[1]
    return total, in_progress, in_stream_queue
end;
function check_requests_stats_per_thread_using_call(thread_count)
    local total, in_progress, in_stream_queue = 0, 0, 0
    for thread_id = 1, thread_count do
        local total_c, in_progress_c, in_stream_queue_c =
        get_network_requests_stats_for_thread_using_call(
            thread_id, "current"
        )
        assert(total_c == in_progress_c + in_stream_queue_c)
        total = total + total_c
        in_progress = in_progress + in_progress_c
        in_stream_queue = in_stream_queue + in_stream_queue_c
    end
    assert(total == in_progress + in_stream_queue)
    return total, in_progress, in_stream_queue
end;
function check_requests_stats_per_thread_using_index(thread_count)
    local total, in_progress, in_stream_queue = 0, 0, 0
    for thread_id = 1, thread_count do
        local total_c, in_progress_c, in_stream_queue_c =
        get_network_requests_stats_for_thread_using_index(
            thread_id, "current"
        )
        assert(total_c == in_progress_c + in_stream_queue_c)
        total = total + total_c
        in_progress = in_progress + in_progress_c
        in_stream_queue = in_stream_queue + in_stream_queue_c
    end
    assert(total == in_progress + in_stream_queue)
    return total, in_progress, in_stream_queue
end;
test_run:cmd("setopt delimiter ''");

-- We check that statistics gathered per each thread in sum is equal to
-- statistics gathered from all threads.
thread_count = 5
test_run:cmd(string.format("start server test with args=\"%s\"", thread_count))
test_run:switch("test")
fiber = require('fiber')
cond = fiber.cond()
function wait() cond:wait() end
function wakeup() cond:signal() end
test_run:switch("default")

server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]
conn = net_box.new(server_addr)
stream = conn:new_stream()
request_count = 10
service_total_msg_count, service_in_progress_msg_count, \
service_stream_msg_count = \
    get_network_requests_stats_using_call("total");

test_run:cmd("setopt delimiter ';'")
for i = 1, request_count do
    stream:call("wait", {}, {is_async = true})
end;
conn:close();
-- Here we check a few things for 'current' request statistics:
-- - statistics getting by call same as getting by index
-- - total statistics is equal to the sum of statistics per threads
-- - total request count is equal to the sum of in progress request_count
--   count + in stream queue request count (we can ignore the number of
--   requests in the cbus queue, it is always 0, since there are only one
--   request in progress and it do not have time to get stuck.
for i = 1, request_count do
    local total_c_call, in_progress_c_call, in_stream_queue_c_call =
          get_network_requests_stats_using_call("current")
    local total_c_index, in_progress_c_index, in_stream_queue_c_index =
          get_network_requests_stats_using_index("current")
    assert(
        total_c_call == total_c_index and
        in_progress_c_call == in_progress_c_index and
        in_stream_queue_c_call == in_stream_queue_c_index
    )
    assert(total_c_call == request_count - (i - 1))
    assert(in_progress_c_call == 1)
    assert(in_stream_queue_c_call == request_count - i)
    assert(
        total_c_call ==
        in_progress_c_call + in_stream_queue_c_call
    )
    local total_thd_c_call, in_progress_thd_c_call,
          in_stream_queue_thd_c_call =
          check_requests_stats_per_thread_using_call(thread_count)
    local total_thd_c_index, in_progress_thd_c_index,
          in_stream_queue_thd_c_index =
          check_requests_stats_per_thread_using_index(thread_count)
    assert(
        total_thd_c_call == total_thd_c_index and
        in_progress_thd_c_call == in_progress_thd_c_index and
        in_stream_queue_thd_c_call == in_stream_queue_thd_c_index
    )
    assert(
        total_thd_c_call == total_c_call and
        in_progress_thd_c_call == in_progress_c_call and
        in_stream_queue_thd_c_call == in_stream_queue_c_call
    )
    test_run:cmd("eval test 'wakeup()'")
end;
total, in_progress, in_stream_queue = 0, 0, 0;
-- Here we check a few things for 'total' request statistics:
-- - statistics getting by call same as getting by index
-- - total statistics is equal to the sum of statistics per threads
for thread_id = 1, thread_count do
    local total_thd_t_call, in_progress_thd_t_call,
          in_stream_queue_thd_t_call =
          get_network_requests_stats_for_thread_using_call(thread_id, "total")
    local total_thd_t_index, in_progress_thd_t_index,
          in_stream_queue_thd_t_index =
          get_network_requests_stats_for_thread_using_index(thread_id, "total")
    assert(
        total_thd_t_call == total_thd_t_index and
        in_progress_thd_t_call == in_progress_thd_t_index and
        in_stream_queue_thd_t_call == in_stream_queue_thd_t_index
    )
    total = total + total_thd_t_call
    in_progress = in_progress + in_progress_thd_t_call
    in_stream_queue = in_stream_queue + in_stream_queue_thd_t_call
end;
total_t_call, in_progress_t_call, in_stream_queue_t_call =
    get_network_requests_stats_using_call("total");
total_t_index, in_progress_t_index, in_stream_queue_t_index =
    get_network_requests_stats_using_index("total");
assert(
    total_t_call == total_t_index and
    in_progress_t_call == in_progress_t_index and
    in_stream_queue_t_call == in_stream_queue_t_index
);
assert(
    total == total_t_call and
    in_progress == in_progress_t_call and
    in_stream_queue == in_stream_queue_t_call
);
-- We do not take into account service messages which was sent
-- when establishing a connection
assert(
    total_t_call == request_count + service_total_msg_count and
    in_progress_t_call  == request_count + service_in_progress_msg_count
);
-- Check that box.stat.net.thread[i] does not crash for incorrect i
test_run:cmd("eval test 'return box.stat.net.thread[0]'");
test_run:cmd(string.format("eval test 'return box.stat.net.thread[%d]'", thread_count + 1));

test_run:cmd("setopt delimiter ''");

test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
