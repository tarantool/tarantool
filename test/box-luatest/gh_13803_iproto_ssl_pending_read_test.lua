local fiber = require('fiber')
local fio = require('fio')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local CERT_DIR = fio.pathjoin(fio.abspath(os.getenv('SOURCEDIR') or '.'),
                              'test/ssl_cert')

-- The load must pipeline more than readahead (16320 bytes) on a connection
-- to reproduce the stall. Increase it if a host doesn't reproduce it.
local CONNECTIONS = 4
local FIBERS_PER_CONNECTION = 16
local PAYLOAD_SIZE = 1024
local LOAD_SECONDS = 1.2

g.before_all(function(cg)
    cg.main_uri = server.build_listen_uri('ssl_pending_read_main')
    cg.tls_uri = server.build_listen_uri('ssl_pending_read_tls')
    cg.server = server:new({
        alias = 'master',
        box_cfg = {
            listen = {cg.main_uri, {uri = cg.tls_uri, params = {
                transport = 'ssl',
                ssl_cert_file = fio.pathjoin(CERT_DIR, 'server.crt'),
                ssl_key_file = fio.pathjoin(CERT_DIR, 'server.key'),
            }}},
            -- Less than a full TLS record, which is 16384 bytes.
            readahead = 16320,
        },
        net_box_uri = cg.main_uri,
    })
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'echo', function(payload) return payload end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Makes pipelined calls from several fibers on each of several SSL
-- connections and checks that all of them succeed and finish soon after the
-- load stops.
local function check_no_stall(cg, rounds)
    local payload = string.rep('x', PAYLOAD_SIZE)
    local calls = CONNECTIONS * FIBERS_PER_CONNECTION
    for round = 1, rounds do
        local conns, stop, closed = {}, false, false
        local succeeded, failed, last_error = 0, 0, nil
        for i = 1, CONNECTIONS do
            conns[i] = net.connect({uri = cg.tls_uri,
                                    params = {transport = 'ssl'}})
            t.assert_equals(conns[i].state, 'active')
        end
        local finished = fiber.channel(calls)
        for i = 1, CONNECTIONS do
            for _ = 1, FIBERS_PER_CONNECTION do
                fiber.create(function()
                    while not stop do
                        local ok, res = pcall(conns[i].call, conns[i], 'echo',
                                              {payload}, {timeout = 10})
                        if ok and res == payload then
                            succeeded = succeeded + 1
                        else
                            if not closed then
                                failed = failed + 1
                                last_error = ok and 'unexpected result' or res
                            end
                            break
                        end
                    end
                    finished:put(true)
                end)
            end
        end
        fiber.sleep(LOAD_SECONDS)
        stop = true
        local waiting = calls
        local deadline = fiber.clock() + 5
        while waiting > 0 and fiber.clock() < deadline do
            if finished:get(0.1) ~= nil then
                waiting = waiting - 1
            end
        end
        closed = true
        for i = 1, CONNECTIONS do
            conns[i]:close()
        end
        t.assert_equals(waiting, 0, string.format(
            'round %d: calls still waiting 5s after the load stopped, ' ..
            '%d failed', round, failed))
        t.assert_equals(failed, 0, string.format(
            'round %d: calls failed, last error: %s', round, last_error))
        t.assert_gt(succeeded, 0)
    end
end

--
-- Checks that pipelined requests over SSL don't wait for the next request
-- once the load stops (gh-13803).
--
g.test_pipelined_calls = function(cg)
    check_no_stall(cg, 6)
end

g.after_test('test_pipelined_calls_with_low_net_msg_max', function(cg)
    cg.server:exec(function(net_msg_max)
        box.cfg({net_msg_max = net_msg_max})
    end, {cg.net_msg_max})
    cg.net_msg_max = nil
end)

--
-- Checks that pipelined requests over SSL don't stall when net_msg_max makes
-- the server stop and resume reading the connections (gh-13803).
--
g.test_pipelined_calls_with_low_net_msg_max = function(cg)
    cg.net_msg_max = cg.server:exec(function()
        local net_msg_max = box.cfg.net_msg_max
        box.cfg({net_msg_max = 4})
        return net_msg_max
    end)
    check_no_stall(cg, 3)
end
