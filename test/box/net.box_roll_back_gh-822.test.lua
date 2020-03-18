remote = require 'net.box'
test_run = require('test_run').new()

--
-- gh-822: net.box.call should roll back local transaction on error
--

_ = box.schema.space.create('gh822')
_ = box.space.gh822:create_index('primary')

test_run:cmd("setopt delimiter ';'")

-- rollback on invalid function
function rollback_on_invalid_function()
    box.begin()
    box.space.gh822:insert{1, "netbox_test"}
    pcall(remote.self.call, remote.self, 'invalid_function')
    return box.space.gh822:get(1) == nil
end;
rollback_on_invalid_function();

-- rollback on call error
function test_error() error('Some error') end;
function rollback_on_call_error()
    box.begin()
    box.space.gh822:insert{1, "netbox_test"}
    pcall(remote.self.call, remote.self, 'test_error')
    return box.space.gh822:get(1) == nil
end;
rollback_on_call_error();

-- rollback on eval
function rollback_on_eval_error()
    box.begin()
    box.space.gh822:insert{1, "netbox_test"}
    pcall(remote.self.eval, remote.self, "error('Some error')")
    return box.space.gh822:get(1) == nil
end;
rollback_on_eval_error();

test_run:cmd("setopt delimiter ''");
box.space.gh822:drop()
