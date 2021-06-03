yaml = require('yaml')
fiber = require('fiber')
test_run = require('test_run').new()

stack_len = 0
stack_str = ""

test_run:cmd('setopt delimiter ";"')
foo1 = function() foo2() end;
foo2 = function() foo3() end;

foo3 = function()
    local id = fiber.self():id()
    local info = fiber.info()[id]
    local stack = info.backtrace
    stack_len = stack and #stack or -1
    stack_str = yaml.encode(stack)
end;

test_run:cmd('setopt delimiter ""');

local bar,baz

bar = function(n) if n ~= 0 then baz(n - 1) else fiber.create(foo1) end end
baz = function(n) fiber.create(bar, n) end

baz(5)
init_stack_len = stack_len

if stack_len ~= -1 then fiber:parent_bt_enable() end
baz(5)
with_parent_stack_len = stack_len
assert(with_parent_stack_len > 0 or init_stack_len == -1)
assert(stack_str:find("foo2") ~= nil or init_stack_len == -1)
assert(stack_str:find("baz") ~= nil or init_stack_len == -1)

if stack_len ~= -1 then fiber:parent_bt_disable() end
baz(5)
without_parent_stack_len = stack_len
assert(without_parent_stack_len > 0 or init_stack_len == -1)
assert(without_parent_stack_len == init_stack_len)
assert(without_parent_stack_len < with_parent_stack_len or init_stack_len == -1)
assert(stack_str:find("foo2") ~= nil or init_stack_len == -1)
assert(stack_str:find("baz") == nil)
