build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/box/?.so;'..build_path..'/test/box/?.dylib;'..package.cpath

log = require('log')
net = require('net.box')

c = net.connect(os.getenv("LISTEN"))

box.schema.func.create('function1', {language = "C"})
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
box.schema.func.drop("function1.args")

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

c:close()
