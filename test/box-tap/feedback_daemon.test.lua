#!/usr/bin/env tarantool

-- Testing feedback module

local tap = require('tap')
local json = require('json')
local fiber = require('fiber')
local test = tap.test('feedback_daemon')
local socket = require('socket')

box.cfg{log = 'report.log', log_level = 6}

local feedback_save = nil
local feedback = nil
local feedback_count = 0

local function feedback_reset()
    feedback = nil
    feedback_count = 0
end

local function http_handle(s)
    -- When data is > 1024 bytes, curl sends "Expect: 100-continue" header,
    -- and waits for this response before sending the actual data.
    s:write("HTTP/1.1 100 Continue\r\n\r\n")
    s:write("HTTP/1.1 200 OK\r\n")
    s:write("Accept: */*\r\n")
    s:write("Connection: keep-alive\r\n")
    s:write("Access-Control-Allow-Origin: *\r\n")
    s:write("Access-Control-Allow-Credentials: true\r\n")
    s:write("Content-Length: 0\r\n\r\n")

    local buf = s:read('\n')
    local length
    while not buf:match("^\r\n") do
        if buf:lower():match("content%-length") then
            length = tonumber(buf:split(": ")[2])
        end
        buf = s:read('\n')
    end
    buf = s:read(length)
    local ok = pcall(json.decode, buf)
    if ok then
        feedback = buf
        feedback_count = feedback_count + 1
    end
end

local server = socket.tcp_server("127.0.0.1", 0, http_handle)
local port = server:name().port

local interval = 0.01
local ok, err = pcall(box.cfg, {
    feedback_host = string.format("127.0.0.1:%i", port),
    feedback_interval = interval,
})

if not ok then
    --
    -- gh-3308: feedback daemon is an optional pre-compile time
    -- defined feature, depending on CMake flags. It is not
    -- present always. When it is disabled, is should not be
    -- visible anywhere.
    --
    test:plan(2)
    test:diag('Feedback daemon is supposed to be disabled')
    test:like(err, 'unexpected option', 'Feedback options are undefined')
    test:isnil(box.feedback, 'No feedback daemon - no its API')
    test:check()
    os.exit(0)
end

test:plan(30)

local function check(message)
    while feedback_count < 1 do
        fiber.sleep(0.001)
    end
    test:ok(feedback ~= nil, message)
    feedback_save = feedback
    feedback_reset()
end

check("initial check")
local daemon = box.internal.feedback_daemon
-- check if feedback has been sent and received
daemon.reload()
check("feedback received after reload")

local errinj = box.error.injection
errinj.set("ERRINJ_HTTPC", true)
feedback_reset()
errinj.set("ERRINJ_HTTPC", false)
check('feedback received after errinj')

daemon.send()
check("feedback received after explicit sending")

box.cfg{feedback_enabled = false}
feedback_reset()
daemon.send()
test:ok(feedback_count == 0, "no feedback after disabling")

box.cfg{feedback_enabled = true}
daemon.send()
check("feedback after start")

daemon.stop()
feedback_reset()
daemon.send()
test:ok(feedback_count == 0, "no feedback after stop")

daemon.start()
daemon.send()
check("feedback after start")
daemon.send()
check("feedback after feedback send")

daemon.stop()

box.feedback.save("feedback.json")
local fio = require("fio")
local fh = fio.open("feedback.json")
test:ok(fh, "file is created")
local file_data = fh:read()
-- Ignore the report time and uptime. The data should be equal other than that.
feedback_save = string.gsub(feedback_save, '"uptime":(%d+)', '"uptime":0')
file_data = string.gsub(file_data, '"uptime":(%d+)', '"uptime":0')
feedback_save = string.gsub(feedback_save, '"time":(%d+)', '"time":0')
file_data = string.gsub(file_data, '"time":(%d+)', '"time":0')
test:is(file_data, feedback_save, "data is equal")
fh:close()
fio.unlink("feedback.json")

server:close()
-- check it does not fail without server
daemon = box.internal.feedback_daemon
daemon.start()
daemon.send()
daemon.stop()

--
-- gh-3608: OS version and containerization detection in feedback daemon
--

local actual = daemon.generate_feedback()
test:is(type(actual.os), 'string', 'feedback contains "os" key')
test:is(type(actual.arch), 'string', 'feedback contains "arch" key')
test:is(type(actual.cgroup), 'string', 'feedback contains "cgroup" key')

--
-- gh-4943: Collect engines and indices statistics.
--

local actual = daemon.generate_feedback()
test:is(type(actual.features), 'table', 'features field is present')
test:is(type(actual.features.schema), 'table', 'schema stats are present')
local expected = {
    memtx_spaces = 0,
    vinyl_spaces = 0,
    temporary_spaces = 0,
    local_spaces = 0,
    sync_spaces = 0,
    tree_indices = 0,
    rtree_indices = 0,
    hash_indices = 0,
    bitset_indices = 0,
    jsonpath_indices = 0,
    jsonpath_multikey_indices = 0,
    functional_indices = 0,
    functional_multikey_indices = 0,
}
test:is_deeply(actual.features.schema, expected,
        'schema stats are empty at the moment')

box.schema.create_space('features_vinyl', {engine = 'vinyl', if_not_exists = true})
box.schema.create_space('features_memtx_empty', {if_not_exists = true})
box.schema.create_space('features_memtx',
        {engine = 'memtx', is_local = true, temporary = true, if_not_exists = true})
box.space.features_vinyl:create_index('vinyl_pk', {type = 'tree', if_not_exists = true})
box.space.features_memtx:create_index('memtx_pk', {type = 'tree', if_not_exists = true})
box.space.features_memtx:create_index('memtx_hash', {type = 'hash', if_not_exists = true})
box.space.features_memtx:create_index('memtx_bitset', {type = 'bitset', if_not_exists = true})
box.schema.create_space('features_sync', {is_sync=true, if_not_exists=true})
box.space.features_memtx:create_index('memtx_rtree',
        {type = 'rtree', parts = {{field = 3, type = 'array'}}, if_not_exists = true})
box.space.features_memtx:create_index('memtx_jpath',
        {parts = {{field = 4, type = 'str', path = 'data.name'}}, if_not_exists = true})
box.space.features_memtx:create_index('memtx_multikey',
        {parts = {{field = 5, type = 'str', path = 'data[*].name'}}, if_not_exists = true})
box.schema.func.create('features_func', {
    body = "function(tuple) return {string.sub(tuple[2], 1, 1)} end",
    is_deterministic = true,
    is_sandboxed = true,
    if_not_exists = true})
box.schema.func.create('features_func_multikey', {
    body = "function(tuple) return {1, 2} end",
    is_deterministic = true,
    is_sandboxed = true,
    opts = {is_multikey = true},
    if_not_exists = true})
box.space.features_memtx:create_index('functional',
        {parts = {{field = 1, type = 'number'}}, func = 'features_func', if_not_exists = true})
box.space.features_memtx:create_index('functional_multikey',
        {parts = {{field = 1, type = 'number'}}, func = 'features_func_multikey', if_not_exists = true})

actual = daemon.generate_feedback()
local schema_stats = actual.features.schema
test:test('features.schema', function(t)
    t:plan(13)
    t:is(schema_stats.memtx_spaces, 3, 'memtx engine usage gathered')
    t:is(schema_stats.vinyl_spaces, 1, 'vinyl engine usage gathered')
    t:is(schema_stats.temporary_spaces, 1, 'temporary space usage gathered')
    t:is(schema_stats.local_spaces, 1, 'local space usage gathered')
    t:is(schema_stats.sync_spaces, 1, 'sync space usage gathered')
    t:is(schema_stats.tree_indices, 6, 'tree index gathered')
    t:is(schema_stats.hash_indices, 1, 'hash index gathered')
    t:is(schema_stats.rtree_indices, 1, 'rtree index gathered')
    t:is(schema_stats.bitset_indices, 1, 'bitset index gathered')
    t:is(schema_stats.jsonpath_indices, 2, 'jsonpath index gathered')
    t:is(schema_stats.jsonpath_multikey_indices, 1, 'jsonpath multikey index gathered')
    t:is(schema_stats.functional_indices, 2, 'functional index gathered')
    t:is(schema_stats.functional_multikey_indices, 1, 'functional multikey index gathered')
end)

box.space.features_memtx:create_index('memtx_sec', {type = 'hash', if_not_exists = true})

actual = daemon.generate_feedback()
test:is(actual.features.schema.hash_indices, 2,
        'internal cache invalidates when schema changes')

--
-- collect box.cfg options: election mode, synchronous replication and tx
-- manager.
--
local em = box.cfg.election_mode
local quorum = box.cfg.replication_synchro_quorum
box.cfg{election_mode='candidate', replication_synchro_quorum=2}
box.on_reload_configuration = function() end
actual = daemon.generate_feedback()
test:is(actual.features.on_reload_configuration_used, true, 'on_reload_configuration reported')
test:is(actual.options.election_mode, 'candidate', 'election_mode option reported')
test:is(actual.options.replication_synchro_quorum, 2, 'replication_synchro_quorum option reported')
test:is(actual.options.memtx_use_mvcc_engine, false, 'memtx_use_mvcc_engine option reported')
box.cfg{election_mode=em, replication_synchro_quorum=quorum}

box.space.features_vinyl:drop()
box.space.features_memtx_empty:drop()
box.space.features_memtx:drop()
box.space.features_sync:drop()

local function check_stats(stat)
    local sub = test:test('feedback operation stats')
    sub:plan(18)
    local box_stat = box.stat()
    local net_stat = box.stat.net()
    for op, val in pairs(box_stat) do
        sub:is(stat.box[op].total, val.total,
               string.format('%s total is reported', op))
    end
    for op, val in pairs(net_stat) do
        sub:is(stat.net[op].total, val.total,
               string.format('%s total is reported', op))
        if val.current ~= nil then
            sub:is(stat.net[op].current, val.current,
                   string.format('%s current is reported', op))
        end
    end
    sub:check()
end

actual = daemon.generate_feedback()
test:is(fiber.time64(), actual.stats.time, "Time of report generation is correct")

-- Check that all the statistics are reported.
check_stats(actual.stats)

box.schema.space.create('test')
box.space.test:create_index('pk')
box.space.test:insert{1}
box.space.test:select{}
box.space.test:update({1}, {{'=', 2, 1}})
box.space.test:replace{2}
box.space.test:delete{1}
box.space.test:drop()

-- Check that all the statistics are updated.
actual = daemon.generate_feedback()
test:is(fiber.time64(), actual.stats.time, "Time of report generation is correct")

check_stats(actual.stats)

actual = daemon.generate_feedback()
test:is(box.info.uptime, actual.uptime, "Server uptime is reported and is correct.")

daemon.reload()
actual = daemon.generate_feedback()

local events_expected = {}
test:is_deeply(actual.events, events_expected, "Events are empty initially.")

box.schema.space.create('test')
box.space.test:create_index('pk')
box.space.test.index.pk:drop()
box.space.test:drop()

actual = daemon.generate_feedback()
events_expected = {
    create_space = 1,
    create_index = 1,
    drop_space = 1,
    drop_index = 1,
}

test:is_deeply(actual.events, events_expected, "Events are counted correctly")

test:check()
os.exit(0)
