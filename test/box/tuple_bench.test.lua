package.cpath = '../box/?.so;../box/?.dylib;'..package.cpath

net = require('net.box')

c = net:new(os.getenv("LISTEN"))

box.schema.func.create('tuple_bench', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'tuple_bench')
space = box.schema.space.create('tester')
_ = space:create_index('primary', {type = 'TREE', parts =  {1, 'NUM', 2, 'STR'}})
box.schema.user.grant('guest', 'read,write', 'space', 'tester')

box.space.tester:insert({1, "abc", 100})
box.space.tester:insert({2, "bcd", 200})
box.space.tester:insert({3, "ccd", 200})

prof = require('gperftools.cpu')
prof.start('tuple.prof')

c:call('tuple_bench')

prof.flush()
prof.stop()

box.schema.func.drop("tuple_bench")

box.space.tester:drop()
