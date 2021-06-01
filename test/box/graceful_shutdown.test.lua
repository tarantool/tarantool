net_box = require('net.box')
fiber = require('fiber')
env = require('test_run')
test_run = env.new()

------------------------------------------------------------
-- Upon receiving a shutdown signal server
-- should stop accept new connections,
-- close for read existing not graceful connections
-- and do some work before the shutdown is complete
------------------------------------------------------------

box.schema.user.grant('guest','execute,write,read','universe', nil, {if_not_exists = true})

_ = box.schema.space.create('counter')
_ = box.space.counter:create_index('primary')

test_run:cmd("create server remote with script='box/graceful_shutdown.lua'")
test_run:cmd("start server remote")
test_run:cmd("switch remote")
net_box = require('net.box')
test_run:cmd("set variable def_uri to 'default.listen'")
def_con = net_box.connect(def_uri)
test_run:cmd("setopt delimiter ';'")
function check(t)
    require('fiber').sleep(t)
    def_con:call('box.space.counter:auto_increment',
                 {{'sleep ' .. tostring(t)}})
end;
test_run:cmd("setopt delimiter ''");
test_run:cmd("switch default")

test_run:cmd("set variable remote_uri to 'remote.listen'")
remote_con = net_box.connect(remote_uri)
test_run:cmd("setopt delimiter ';'")
remote_con:set_shutdown_handler(function()
    box.space.counter:auto_increment{'unreachable'}
end);
graceful_con = net_box.connect(remote_uri);
graceful_con.space._session_settings:update('graceful_shutdown',
                                            {{'=', 'value', true}});
graceful_con:set_shutdown_handler(function()
    box.space.counter:auto_increment{'shutdown receive'}
end);
graceful_shutdowned = net_box.connect(remote_uri);
graceful_shutdowned.space._session_settings:update('graceful_shutdown',
                                            {{'=', 'value', true}});
graceful_shutdowned:shutdown();
-- Connetion must be shutdowned by server after getting IPROTO_SHUTDOWN
-- form client.
graceful_shutdowned:ping();
graceful_shutdowned:set_shutdown_handler(function()
    box.space.counter:auto_increment{'unreachable'}
end);

for time = 1.5, 10, 3 do
    remote_con:call('check', {time}, {is_async=true})
end;
test_run:cmd("setopt delimiter ''");

fiber.sleep(0.1)
_ = remote_con:call('os.exit', {}, { is_async=true })
fiber.sleep(0.5)

--
-- Remote started to shutdown, so it isn't accept new connections
-- and doesn't response to requests from existing connections.
--
net_box.connect(remote_uri).state
remote_con:ping()
graceful_con:ping()
graceful_con:shutdown()
graceful_con:ping()

--
-- shutdown_handler of remote_con must not be called
-- shutdown_handler of graceful_con must be done
-- shutdown_handler of graceful_shutdowned must not be called
-- check(1,5) must be done
-- check(4,5) must be cancelled by expiring of on_shutdown_trigger_timeout
--
--
fiber.sleep(5)
box.space.counter:select()
box.space.counter:drop()
test_run:cmd("stop server remote")

---------------------------------------------------------------
-- Connections that support graceful shutdown can has different
-- handlers. Handler of not graceful connections isn't called
-- on shutdown.
---------------------------------------------------------------

test_run:cmd("start server remote")
test_run:cmd("set variable remote_uri to 'remote.listen'")
graceful_cons = {}
cons = {}
channel = fiber.channel(200)
channel_sum = 0
con_handler_invoked = false
test_run:cmd("setopt delimiter ';'")
for i=1,100 do
    graceful_con = net_box.connect(remote_uri)
    con = net_box.connect(remote_uri)
    graceful_con.space._session_settings:update('graceful_shutdown',
                                                {{'=', 'value', true}})
    graceful_con:set_shutdown_handler(function() channel:put(i) end)
    con:set_shutdown_handler(function() channel:put(0) end)
    graceful_cons[i] = graceful_con
    cons[i] = con
end;
-- Synchronous request to wait for completion of shutdown.
net_box.connect(remote_uri):call('os.exit');
for i=1,100 do
    local msg = 0
    while msg == 0 and not con_handler_invoked do
        msg = channel:get()
        channel_sum = channel_sum + msg
        if msg == 0 then
            con_handler_invoked = true
        end
    end
end;
test_run:cmd("setopt delimiter ''");
con_handler_invoked

-- 1 + 2 + ... + 100 = 5050

channel_sum
graceful_cons = nil
cons = nil

test_run:cmd("stop server remote")

---------------------------------------------------------------
-- Active connections counter works with a large number of
-- consecutive connection closes.
---------------------------------------------------------------
test_run:cmd("start server remote")
test_run:cmd("set variable remote_uri to 'remote.listen'")
table = {}
test_run:cmd("setopt delimiter ';'")
for i = 1, 100 do
    graceful_con = net_box.connect(remote_uri)
    graceful_con.space._session_settings:update('graceful_shutdown',
                                                {{'=', 'value', true}})
    table[#table + 1] = graceful_con
end;

for i = 1, #table do
    table[i]:close()
end;
test_run:cmd("setopt delimiter ''");
-- Synchronous request to wait for completion of shutdown.
net_box.connect(remote_uri):call('os.exit')
net_box.connect(remote_uri):ping()
table = nil
test_run:cmd("stop server remote")

---------------------------------------------------------------
-- os.exit inside sequence of connects doesn't lead to infinity
-- shutdown process.
---------------------------------------------------------------

test_run:cmd("start server remote")
test_run:cmd("set variable remote_uri to 'remote.listen'")
table = {}
test_run:cmd("setopt delimiter ';'")
_ = net_box.connect(remote_uri):eval("require('fiber').sleep(0.1) os.exit()",
                                     {}, {is_async=true});
for i = 1, 100 do
    con = net_box.connect(remote_uri)
    table[i] = con
end;
test_run:cmd("setopt delimiter ''");
fiber.sleep(0.5)
-- server must be stopped
net_box.connect(remote_uri):ping()
table = nil
test_run:cmd("stop server remote")

---------------------------------------------------------------
-- Requests aren't lost on shutdown process.
---------------------------------------------------------------

test_run:cmd("start server remote")
test_run:cmd("set variable remote_uri to 'remote.listen'")
table = {}
test_run:cmd("setopt delimiter ';'")
for i = 1, 100 do
    graceful_con = net_box.connect(remote_uri)
    graceful_con.space._session_settings:update('graceful_shutdown',
                                                {{'=', 'value', true}})
    table[i] = graceful_con
end;

wait_ping = fiber.channel(100);

for i = 1,10 do
    fiber.create(function()
        for i = 1, 100 do
            table[i]:ping()
            fiber.yield()
        end
        wait_ping:put(true)
    end)
end;

test_run:cmd("setopt delimiter ''");
net_box.connect(remote_uri):call("os.exit")
for i=1,10 do wait_ping:get() end
table = nil
test_run:cmd("stop server remote")

box.schema.user.revoke('guest','execute,write,read','universe')

test_run:cmd("cleanup server remote")
test_run:cmd("delete server remote")
test_run:cmd("restart server default")
