env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

decimal = require('decimal')

_ = box.schema.space.create('test', {engine=engine})
_ = box.space.test:create_index('pk')
box.space.test:insert{1, decimal.new(1.1)}
box.space.test:insert{2, decimal.new(2.2)}
box.space.test:insert{3, decimal.new(1.1)}
box.space.test:insert{4, decimal.new('1234567890123456789.9876543210987654321'), decimal.new(1.2345)}
box.space.test:select{}
a = box.space.test:get{4}
a:next()
a:next(1)
a:next(2)
a:slice(-2)
box.space.test:replace{3, decimal.new(3.3)}
box.space.test:select{}
box.space.test:drop()
