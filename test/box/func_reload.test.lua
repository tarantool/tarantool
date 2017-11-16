fio  = require('fio')
net = require('net.box')
fiber = require('fiber')

ext = (jit.os == "OSX" and "dylib" or "so")
build_path = os.getenv("BUILDDIR")
reload1_path = build_path..'/test/box/reload1.'..ext
reload2_path = build_path..'/test/box/reload2.'..ext
reload_path = "reload."..ext
_ = fio.unlink(reload_path)

c = net.connect(os.getenv("LISTEN"))

box.schema.func.create('reload.foo', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'reload.foo')
_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary', {parts = {1, "integer"}})
box.schema.user.grant('guest', 'read,write', 'space', 'test')
_ = fio.unlink(reload_path)
fio.symlink(reload1_path, reload_path)

--check not fail on non-load func
box.schema.func.reload("reload.foo")

-- test of usual case reload. No hanging calls
box.space.test:insert{0}
c:call("reload.foo", {1})
box.space.test:delete{0}
_ = fio.unlink(reload_path)
fio.symlink(reload2_path, reload_path)

box.schema.func.reload("reload.foo")
c:call("reload.foo")
box.space.test:select{}
box.space.test:truncate()

-- test case with hanging calls
_ = fio.unlink(reload_path)
fio.symlink(reload1_path, reload_path)
box.schema.func.reload("reload.foo")

fibers = 10
for i = 1, fibers do fiber.create(function() c:call("reload.foo", {i}) end) end

while box.space.test:count() < fibers do fiber.sleep(0.001) end
-- double reload doesn't fail waiting functions
box.schema.func.reload("reload.foo")

_ = fio.unlink(reload_path)
fio.symlink(reload2_path, reload_path)
box.schema.func.reload("reload.foo")
c:call("reload.foo")

while box.space.test:count() < 2 * fibers + 1 do fiber.sleep(0.001) end
box.space.test:select{}
box.schema.func.drop("reload.foo")
box.space.test:drop()
_ = fio.unlink(reload_path)

fio.symlink(reload1_path, reload_path)
box.schema.func.create('reload.test_reload', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'reload.test_reload')
s = box.schema.space.create('test_reload')
_ = s:create_index('pk')
box.schema.user.grant('guest', 'read,write', 'space', 'test_reload')
ch = fiber.channel(2)
-- call first time to load function
c:call("reload.test_reload")
s:delete({1})
_ = fio.unlink(reload_path)
fio.symlink(reload2_path, reload_path)
_ = fiber.create(function() ch:put(c:call("reload.test_reload")) end)
while s:get({1}) == nil do fiber.yield(0.0001) end
box.schema.func.reload("reload.test_reload")
_ = fiber.create(function() ch:put(c:call("reload.test_reload")) end)
ch:get()
ch:get()
s:drop()

box.schema.func.create('reload.test_reload_fail', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'reload.test_reload_fail')
c:call("reload.test_reload_fail")
_ = fio.unlink(reload_path)
fio.symlink(reload1_path, reload_path)
s, e = pcall(box.schema.func.reload, "reload.test_reload")
s, string.find(tostring(e), 'test_reload_fail') ~= nil
c:call("reload.test_reload")
c:call("reload.test_reload_fail")

box.schema.func.drop("reload.test_reload")
box.schema.func.drop("reload.test_reload_fail")
_ = fio.unlink(reload_path)

box.schema.func.reload()
box.schema.func.reload("non-existing")
