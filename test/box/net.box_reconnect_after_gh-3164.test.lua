fiber = require 'fiber'
log = require 'log'
test_run = require('test_run').new()
net = require('net.box')

test_run:cmd("push filter 'peer_uuid: .*' to 'peer_uuid: <UUID>'")
test_run:cmd("push filter 'reconnect_after: .*' to 'reconnect_after: <NUM>'")
test_run:cmd("push filter 'schema_version: .*' to 'schema_version: <NUM>'")
test_run:cmd("push filter 'peer_version_id: .*' to 'peer_version_id: <ID>'")
test_run:cmd("push filter 'port: .*' to 'port: <PORT>'")

test_run:cmd('create server connecter with script = "box/proxy.lua"')

--
-- gh-3164: netbox connection is not closed and garbage collected
-- ever, if reconnect_after is set.
--
test_run:cmd('start server connecter')
test_run:cmd("set variable connect_to to 'connecter.listen'")
weak = setmetatable({}, {__mode = 'v'})
-- Create strong and weak reference. Weak is valid until strong
-- is valid too.
strong = net.connect(connect_to, {reconnect_after = 0.1})
weak.c = strong
weak.c:ping()
test_run:cmd('stop server connecter')
test_run:cmd('cleanup server connecter')
-- Check the connection tries to reconnect at least two times.
old_log_level = box.cfg.log_level
box.cfg{log_level = 6}
log.info(string.rep('a', 1000))
test_run:cmd("setopt delimiter ';'")
while test_run:grep_log('default', 'unix/', 1000) == nil and
      test_run:grep_log('default', 'No such file or directory', 1000) == nil do
       fiber.sleep(0.1)
end;
log.info(string.rep('a', 1000));
while test_run:grep_log('default', 'unix/', 1000) == nil and
      test_run:grep_log('default', 'No such file or directory', 1000) == nil do
       fiber.sleep(0.1)
end;
test_run:cmd("setopt delimiter ''");
box.cfg{log_level = old_log_level}
collectgarbage('collect')
strong.state
strong == weak.c
-- Remove single strong reference. Now connection must be garbage
-- collected.
strong = nil
collectgarbage('collect')
-- Now weak.c is null, because it was weak reference, and the
-- connection is deleted by 'collect'.
weak.c
