test_run = require('test_run').new()
net = require('net.box')

-- gh-1148: test stacked diagnostics.
--
test_run:cmd("setopt delimiter ';'")
stack_err = function()
    local e1 = box.error.new({code = 111, reason = "e1"})
    local e2 = box.error.new({code = 111, reason = "e2"})
    local e3 = box.error.new({code = 111, reason = "e3"})
    assert(e1 ~= nil)
    e2:set_prev(e1)
    assert(e2.prev == e1)
    e3:set_prev(e2)
    box.error(e3)
end;
test_run:cmd("setopt delimiter ''");

box.schema.user.grant('guest', 'read,write,execute', 'universe')
c = net.connect(box.cfg.listen)
f = function(...) return c:call(...) end
r, e3 = pcall(f, 'stack_err')
assert(r == false)
e3
e2 = e3.prev
assert(e2 ~= nil)
e2
e1 = e2.prev
assert(e1 ~= nil)
e1
assert(e1.prev == nil)

box.schema.user.revoke('guest', 'read,write,execute', 'universe')
