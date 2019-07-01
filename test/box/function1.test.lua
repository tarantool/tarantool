build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/box/?.so;'..build_path..'/test/box/?.dylib;'..package.cpath

log = require('log')
net = require('net.box')

c = net.connect(os.getenv("LISTEN"))

box.schema.func.create('function1', {language = "C"})
box.space._func:replace{2, 1, 'function1', 0, 'LUA'}
box.schema.user.grant('guest', 'execute', 'function', 'function1')
_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest', 'read,write', 'space', 'test')

c:call('function1')
box.schema.func.drop("function1")

box.schema.func.create('function1.args', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'function1.args')
c:call('function1.args')
c:call('function1.args', { "xx" })
c:call('function1.args', { 15 })
box.func["function1.args"]
box.func["function1.args"]:call()
box.func["function1.args"]:call({ "xx" })
box.func["function1.args"]:call({ 15 })
box.schema.func.drop("function1.args")
box.func["function1.args"]

box.schema.func.create('function1.multi_inc', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'function1.multi_inc')

c:call('function1.multi_inc')
box.space.test:select{}
c:call('function1.multi_inc', { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 })
box.space.test:select{}
c:call('function1.multi_inc', { 2, 4, 6, 8, 10 })
box.space.test:select{}
c:call('function1.multi_inc', { 0, 2, 4 })
box.space.test:select{}

box.schema.func.drop("function1.multi_inc")

box.schema.func.create('function1.errors', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'function1.errors')
c:call('function1.errors')
box.schema.func.drop("function1.errors")

box.schema.func.create('xxx', {language = 'invalid'})

-- language normalization
function func_lang(name) return (box.space._func.index[2]:select{name}[1] or {})[5] end

box.schema.func.create('f11'),                      func_lang('f11')
box.schema.func.create('f12', {language = 'Lua'}),  func_lang('f12')
box.schema.func.create('f13', {language = 'lua'}),  func_lang('f13')
box.schema.func.create('f14', {language = 'lUa'}),  func_lang('f14')
box.schema.func.create('f15', {language = 'c'}),    func_lang('f15')
box.schema.func.create('f16', {language = 'C'}),    func_lang('f16')

box.schema.func.drop("f11")
box.schema.func.drop("f12")
box.schema.func.drop("f13")
box.schema.func.drop("f14")
box.schema.func.drop("f15")
box.schema.func.drop("f16")

box.space.test:drop()

-- Missing shared library
name = 'unkownmod.unknownfunc'
box.schema.func.create(name, {language = 'C'})
box.schema.user.grant('guest', 'execute', 'function', name)
c:call(name)
box.schema.func.drop(name)

-- Drop function while executing gh-910
box.schema.func.create('function1.test_yield', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'function1.test_yield')
s = box.schema.space.create('test_yield')
_ = s:create_index('pk')
box.schema.user.grant('guest', 'read,write', 'space', 'test_yield')
fiber = require('fiber')
ch = fiber.channel(1)
_ = fiber.create(function() c:call('function1.test_yield') ch:put(true) end)
while s:get({1}) == nil do fiber.yield(0.0001) end
box.schema.func.drop('function1.test_yield')
ch:get()
s:drop()

-- gh-2914: check identifier constraints.
test_run = require('test_run').new()
test_run:cmd("push filter '(.builtin/.*.lua):[0-9]+' to '\\1'")
identifier = require("identifier")
test_run:cmd("setopt delimiter ';'")
--
-- '.' in func name is used to point out path therefore '.' in name
-- itself is prohibited
--
--
identifier.run_test(
	function (identifier)
		if identifier == "." then return end
		box.schema.func.create(identifier, {language = "lua"})
		box.schema.user.grant('guest', 'execute', 'function', identifier)
		rawset(_G, identifier, function () return 1 end)
		local res = pcall(c.call, c, identifier)
		if c:call(identifier) ~= 1 then
			error("Should not fire")
		end
		rawset(_G, identifier, nil)
	end,
	function (identifier)
		if identifier == "." then return end
		box.schema.func.drop(identifier)
	end
);
test_run:cmd("setopt delimiter ''");
c:close()

-- Test registered functions interface.
function divide(a, b) return a / b end
box.schema.func.create("divide")
func = box.func.divide
func.call({4, 2})
func:call(4, 2)
func:call()
func:call({})
func:call({4})
func:call({4, 2})
func:call({4, 2, 1})
func:drop()
func
func.drop()
box.func.divide
func:drop()
func:call({4, 2})
box.internal.func_call('divide', 4, 2)

box.schema.func.create("function1.divide", {language = 'C'})
func = box.func["function1.divide"]
func:call(4, 2)
func:call()
func:call({})
func:call({4})
func:call({4, 2})
func:call({4, 2, 1})
func:drop()
box.func["function1.divide"]
func
func:drop()
func:call({4, 2})
box.internal.func_call('function1.divide', 4, 2)

test_run:cmd("setopt delimiter ';'")
function minmax(array)
	local min = 999
	local max = -1
	for _, v in pairs(array) do
		min = math.min(min, v)
		max = math.max(max, v)
	end
	return min, max
end
test_run:cmd("setopt delimiter ''");
box.schema.func.create("minmax")
func = box.func.minmax
func:call({{1, 2, 99, 3, -1}})
func:drop()
box.func.minmax

-- Test access checks for registered functions.
function secret() return 1 end
box.schema.func.create("secret")
box.func.secret:call({})
function secret_leak() return box.func.secret:call() end
box.schema.func.create('secret_leak')
box.schema.user.grant('guest', 'execute', 'function', 'secret_leak')
conn = net.connect(box.cfg.listen)
conn:call('secret_leak')
conn:close()
box.schema.user.revoke('guest', 'execute', 'function', 'secret_leak')
box.schema.func.drop('secret_leak')
box.schema.func.drop('secret')

test_run:cmd("clear filter")

--
-- Check that function cache is updated synchronously with _func changes.
--
box.begin() box.schema.func.create('test') f = box.func.test box.rollback()
f ~= nil
box.func.test == nil
box.schema.func.create('test')
f = box.func.test
box.begin() box.space._func:delete{f.id} f = box.func.test box.rollback()
f == nil
box.func.test ~= nil
box.func.test:drop()
