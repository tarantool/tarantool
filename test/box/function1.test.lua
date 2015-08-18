package.cpath = '../box/?.so;../box/?.dylib;'..package.cpath

log = require('log')
net = require('net.box')

c = net:new(os.getenv("LISTEN"))

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
c:call('function1.args', "xx")
c:call('function1.args', 15)
box.schema.func.drop("function1.args")

box.schema.func.create('function1.multi_inc', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'function1.multi_inc')

c:call('function1.multi_inc')
box.space.test:select{}
c:call('function1.multi_inc', 1, 2, 3, 4, 5, 6, 7, 8, 9, 10)
box.space.test:select{}
c:call('function1.multi_inc', 2, 4, 6, 8, 10)
box.space.test:select{}
c:call('function1.multi_inc', 0, 2, 4)
box.space.test:select{}

box.schema.func.drop("function1.multi_inc")

box.schema.func.create('function1.errors', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'function1.errors')
c:call('function1.errors')
box.schema.func.drop("function1.errors")

box.space.test:drop()
