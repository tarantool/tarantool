test_run = require('test_run').new()
net = require('net.box')

test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua...\"]:<line>: '")

_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary', {type = 'TREE', parts = {1,'unsigned'}})
_ = box.space.test:create_index('covering', {type = 'TREE', parts = {1,'unsigned',3,'string',2,'unsigned'}})
_ = box.space.test:insert{1, 2, "string"}
box.schema.user.grant('guest', 'read,write', 'space', 'test')
c = net:connect(box.cfg.listen)

-- gh-1729 net.box index metadata incompatible with local metadata
c.space.test.index.primary.parts
c.space.test.index.covering.parts

box.space.test:drop()
