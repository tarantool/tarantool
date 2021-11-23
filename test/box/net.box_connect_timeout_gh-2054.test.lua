fiber = require 'fiber'
test_run = require('test_run').new()
net = require('net.box')

-- Test for connect_timeout > 0 in netbox connect
test_run:cmd("setopt delimiter ';'");
need_stop = false;
greeting =
"Tarantool 1.7.3 (Binary) 29b74bed-fdc5-454c-a828-1d4bf42c639a~~\n" ..
"YcuIch+SfaiyL51uxcumke1V4eCCUQPy76t13ZI5HY8=~~~~~~~~~~~~~~~~~~~\n";
socket = require('socket');
srv = socket.tcp_server('localhost', 0, {
    handler = function(fd)
        local fiber = require('fiber')
        while not need_stop do
            fiber.sleep(0.01)
        end
        fd:write(greeting)
    end
});
port = srv:name().port
-- we must get timeout
nb = net.new('localhost:' .. port, {
    wait_connected = true,
    connect_timeout = 0.1
});
nb.error:find('timed out') ~= nil;
need_stop = true
nb:close();
-- we must get peer closed
nb = net.new('localhost:' .. port, {
    wait_connected = true,
    connect_timeout = 0.2
});
nb.error ~= "Timeout exceeded";
nb:close();
test_run:cmd("setopt delimiter ''");
srv:close()

test_run:cmd("clear filter")
