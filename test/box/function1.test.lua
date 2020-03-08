build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/box/?.so;'..build_path..'/test/box/?.dylib;'..package.cpath

log = require('log')
net = require('net.box')

c = net.connect(os.getenv("LISTEN"))

box.schema.func.create('function1', {language = "C"})
id = box.func["function1"].id
function setmap(tab) return setmetatable(tab, { __serialize = 'map' }) end
datetime = os.date("%Y-%m-%d %H:%M:%S")
box.space._func:replace{id, 1, 'function1', 0, 'LUA', '', 'procedure', {}, 'any', 'none', 'none', false, false, true, {"LUA"}, setmap({}), '', datetime, datetime}
box.space._func:replace{id, 1, 'function1', 0, 'LUA', '', 'function', {}, 'any', 'none', 'reads', false, false, true, {"LUA"}, setmap({}), '', datetime, datetime}
box.space._func:replace{id, 1, 'function1', 0, 'LUA', '', 'function', {}, 'any', 'none', 'none', false, false, false, {"LUA"}, setmap({}), '', datetime, datetime}
box.space._func:replace{id, 1, 'function1', 0, 'LUA', '', 'function', {}, 'data', 'none', 'none', false, false, true, {"LUA"}, setmap({}), '', datetime, datetime}
box.space._func:replace{id, 1, 'function1', 0, 'LUA', '', 'function', {}, 'any', 'none', 'none', false, false, true, {"LUA", "C"}, setmap({}), '', datetime, datetime}
box.space._func:replace{id, 1, 'function1', 0, 'LUA', '', 'function', {}, 'any', 'aggregate', 'none', false, false, true, {"LUA"}, setmap({}), '', datetime, datetime}
box.space._func:replace{id, 1, 'function1', 0, 'LUA', '', 'function', {}, 'any', 'none', 'none', false, false, true, {"LUA"}, setmap({}), '', datetime, datetime}
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

--
-- gh-2233: Invoke Lua functions created outside SQL.
--
box.schema.func.create('WAITFOR', {language = 'SQL_BUILTIN', \
	param_list = {'integer'}, returns = 'integer',exports = {'SQL'}})

test_run:cmd("setopt delimiter ';'")
box.schema.func.create("function1.divide", {language = 'C', returns = 'number',
					    is_deterministic = true,
					    exports = {'LUA'}})
test_run:cmd("setopt delimiter ''");
box.execute('SELECT "function1.divide"()')
box.func["function1.divide"]:drop()
test_run:cmd("setopt delimiter ';'")
box.schema.func.create("function1.divide", {language = 'C', returns = 'number',
					    is_deterministic = true,
					    exports = {'LUA', 'SQL'}})
test_run:cmd("setopt delimiter ''");
box.execute('SELECT "function1.divide"()')
box.execute('SELECT "function1.divide"(6)')
box.execute('SELECT "function1.divide"(6, 3)')
box.func["function1.divide"]:drop()
test_run:cmd("setopt delimiter ';'")
box.schema.func.create("function1.divide", {language = 'C', returns = 'number',
					    param_list = {'number', 'number'},
					    is_deterministic = true,
					    exports = {'LUA', 'SQL'}})
test_run:cmd("setopt delimiter ''");
box.execute('SELECT "function1.divide"()')
box.execute('SELECT "function1.divide"(6)')
box.execute('SELECT "function1.divide"(6, 3, 3)')
box.execute('SELECT "function1.divide"(6, 3)')
box.execute('SELECT "function1.divide"(5, 2)')
box.func["function1.divide"]:drop()

function SUMMARIZE(a, b) return a + b end
test_run:cmd("setopt delimiter ';'")
box.schema.func.create("SUMMARIZE", {language = 'LUA', returns = 'number',
				     is_deterministic = true,
				     param_list = {'number', 'number'},
				     exports = {'LUA', 'SQL'}})
test_run:cmd("setopt delimiter ''");
box.execute('SELECT summarize(1, 2)')
box.func.SUMMARIZE:drop()

test_run:cmd("setopt delimiter ';'")
box.schema.func.create("SUMMARIZE", {language = 'LUA', returns = 'number',
				     body = 'function (a, b) return a + b end',
				     is_deterministic = true,
				     param_list = {'number', 'number'},
				     exports = {'LUA', 'SQL'}})
test_run:cmd("setopt delimiter ''");
box.execute('SELECT summarize(1, 2)')
box.func.SUMMARIZE:drop()

--
-- gh-4113: Valid method to use Lua from SQL
--
box.execute('SELECT lua(\'return 1 + 1\')')
box.execute('SELECT lua(\'return box.cfg\')')
box.execute('SELECT lua(\'return box.cfg()\')')
box.execute('SELECT lua(\'return box.cfg.memtx_memory\')')

-- Test registered functions interface.
function divide(a, b) return a / b end
box.schema.func.create("divide", {comment = 'Divide two values'})
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

-- UDF corner cases for SQL: no value returned, too many values returned
box.execute("SELECT LUA('a = 1 + 1')")
box.execute("SELECT LUA('return 1, 2')")

box.schema.func.create('function1', {language = "C", exports = {'LUA', 'SQL'}})
box.execute("SELECT \"function1\"()")
box.schema.func.drop("function1")

box.schema.func.create('function1.multireturn', {language = "C", exports = {'LUA', 'SQL'}})
box.execute("SELECT \"function1.multireturn\"()")
box.schema.func.drop("function1.multireturn")

--
-- gh-4641: box_return_mp() C API to return arbitrary MessagePack
-- from C functions.
--
name = 'function1.test_return_mp'
box.schema.func.create(name, {language = "C", exports = {'LUA'}})
box.func[name]:call()

box.schema.user.grant('guest', 'super')
-- Netbox:call() returns not the same as local call for C
-- functions, see #4799.
net:connect(box.cfg.listen):call(name)
box.schema.user.revoke('guest', 'super')

box.schema.func.drop(name)

--
-- gh-4182: Introduce persistent Lua functions.
--
test_run:cmd("setopt delimiter ';'")
body = [[function(tuple)
		if type(tuple.address) ~= 'string' then
			return nil, 'Invalid field type'
		end
		local t = tuple.address:upper():split()
		for k,v in pairs(t) do t[k] = v end
		return t
	end
]]
test_run:cmd("setopt delimiter ''");
box.schema.func.create('addrsplit', {body = body, language = "C"})
box.schema.func.create('addrsplit', {is_sandboxed = true, language = "C"})
box.schema.func.create('addrsplit', {is_sandboxed = true})
box.schema.func.create('invalid', {body = "function(tuple) ret tuple"})
box.schema.func.create('addrsplit', {body = body, is_deterministic = true})
box.schema.user.grant('guest', 'execute', 'function', 'addrsplit')
conn = net.connect(box.cfg.listen)
conn:call('addrsplit', {{address = "Moscow Dolgoprudny"}})
box.func.addrsplit:call({{address = "Moscow Dolgoprudny"}})
conn:close()
box.snapshot()
test_run:cmd("restart server default")
test_run = require('test_run').new()
test_run:cmd("push filter '(.builtin/.*.lua):[0-9]+' to '\\1'")
net = require('net.box')
conn = net.connect(box.cfg.listen)
conn:call('addrsplit', {{address = "Moscow Dolgoprudny"}})
box.func.addrsplit:call({{address = "Moscow Dolgoprudny"}})
conn:close()
box.schema.user.revoke('guest', 'execute', 'function', 'addrsplit')
box.func.addrsplit:drop()

-- Test sandboxed functions.
test_run:cmd("setopt delimiter ';'")
body = [[function(number)
		math.abs = math.log
		return math.abs(number)
	end]]
test_run:cmd("setopt delimiter ''");
box.schema.func.create('monkey', {body = body, is_sandboxed = true})
box.func.monkey:call({1})
math.abs(1)
box.func.monkey:drop()

sum = 0
function inc_g(val) sum = sum + val end
box.schema.func.create('call_inc_g', {body = "function(val) inc_g(val) end"})
box.func.call_inc_g:call({1})
assert(sum == 1)
box.schema.func.create('call_inc_g_safe', {body = "function(val) inc_g(val) end", is_sandboxed = true})
box.func.call_inc_g_safe:call({1})
assert(sum == 1)
box.func.call_inc_g:drop()
box.func.call_inc_g_safe:drop()

-- Test persistent function assemble corner cases
box.schema.func.create('compiletime_tablef', {body = "{}"})
box.schema.func.create('compiletime_call_inc_g', {body = "inc_g()"})
assert(sum == 1)

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

-- Check SQL builtins
test_run:cmd("setopt delimiter ';'")
sql_builtin_list = {
	"TRIM", "TYPEOF", "PRINTF", "UNICODE", "CHAR", "HEX", "VERSION",
	"QUOTE", "REPLACE", "SUBSTR", "GROUP_CONCAT", "JULIANDAY", "DATE",
	"TIME", "DATETIME", "STRFTIME", "CURRENT_TIME", "CURRENT_TIMESTAMP",
	"CURRENT_DATE", "LENGTH", "POSITION", "ROUND", "UPPER", "LOWER",
	"IFNULL", "RANDOM", "CEIL", "CEILING", "CHARACTER_LENGTH",
	"CHAR_LENGTH", "FLOOR", "MOD", "OCTET_LENGTH", "ROW_COUNT", "COUNT",
	"LIKE", "ABS", "EXP", "LN", "POWER", "SQRT", "SUM", "TOTAL", "AVG",
	"RANDOMBLOB", "NULLIF", "ZEROBLOB", "MIN", "MAX", "COALESCE", "EVERY",
	"EXISTS", "EXTRACT", "SOME", "GREATER", "LESSER", "SOUNDEX",
	"LIKELIHOOD", "LIKELY", "UNLIKELY", "_sql_stat_get", "_sql_stat_push",
	"_sql_stat_init", "GREATEST", "LEAST"
}
test_run:cmd("setopt delimiter ''");
ok = true
for _, v in pairs(sql_builtin_list) do ok = ok and (box.space._func.index.name:get(v) ~= nil) end
ok == true

box.func.LUA:call({"return 1 + 1"})

box.schema.user.grant('guest', 'execute', 'function', 'SUM')
c = net.connect(box.cfg.listen)
c:call("SUM")
c:close()
box.schema.user.revoke('guest', 'execute', 'function', 'SUM')
box.schema.func.drop("SUM")

-- Introduce function options
box.schema.func.create('test', {body = "function(tuple) return tuple end", is_deterministic = true, opts = {is_multikey = true}})
box.func['test'].is_multikey == true
box.func['test']:drop()
