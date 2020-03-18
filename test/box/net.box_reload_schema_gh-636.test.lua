remote = require 'net.box'
fiber = require 'fiber'
test_run = require('test_run').new()

-- #636: Reload schema on demand
sp = box.schema.space.create('test_old')
_ = sp:create_index('primary')
sp:insert{1, 2, 3}

box.schema.user.grant('guest', 'read', 'space', 'test_old')
con = remote.new(box.cfg.listen)
con:ping()
con.space.test_old:select{}
con.space.test == nil

sp = box.schema.space.create('test')
_ = sp:create_index('primary')
sp:insert{2, 3, 4}

box.schema.user.grant('guest', 'read', 'space', 'test')

con.space.test == nil
con:reload_schema()
con.space.test:select{}

box.space.test:drop()
box.space.test_old:drop()
con:close()

name = string.match(arg[0], "([^,]+)%.lua")
file_log = require('fio').open(name .. '.log', {'O_RDONLY', 'O_NONBLOCK'})
file_log:seek(0, 'SEEK_END') ~= 0

box.schema.user.grant('guest', 'execute', 'universe')
test_run:cmd("setopt delimiter ';'")

_ = fiber.create(
   function()
         local conn = require('net.box').new(box.cfg.listen)
         conn:call('no_such_function', {})
         conn:close()
   end
);
test_run:cmd("setopt delimiter ''");
test_run:wait_log('default', 'ER_NO_SUCH_PROC', nil, 10)
box.schema.user.revoke('guest', 'execute', 'universe')
