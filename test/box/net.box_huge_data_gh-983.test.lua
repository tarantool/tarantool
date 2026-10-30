-- gh-983 selecting a lot of data crashes the server or hangs the
-- connection

-- gh-983 test case: iproto connection selecting a lot of data
_ = box.schema.space.create('test', { temporary = true })
_ = box.space.test:create_index('primary', {type = 'TREE', parts = {1,'unsigned'}})

data1k = "aaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhh"

for i = 0,10000 do box.space.test:insert{i, data1k} end

box.schema.user.grant('guest', 'read', 'space', 'test')
net = require('net.box')
c = net:connect(box.cfg.listen)
r = c.space.test:select(nil, {limit=5000})
box.space.test:drop()
