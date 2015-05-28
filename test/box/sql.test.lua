net_box = require('net.box')

s = box.schema.space.create('test', { id = 0 })
box.schema.user.create('test', { password = 'test' })
box.schema.user.grant('test', 'execute,read,write', 'universe')

conn = net_box:new('test:test@' .. box.cfg.listen)
space = conn.space.test

index = box.space.test:create_index('primary', { type = 'hash' })
conn:ping()

-- xxx: bug  currently selects no rows
space:select{}
space:insert{1, 'I am a tuple'}
space:select{1}

-- currently there is no way to find out how many records
-- a space contains 
space:select{0}
space:select{2}

--# stop server default
--# start server default
net_box = require('net.box')
conn = net_box:new('test:test@' .. box.cfg.listen)
space = conn.space.test

space:select{1}
box.snapshot()
space:select{1}

--# stop server default
--# start server default
net_box = require('net.box')
conn = net_box:new('test:test@' .. box.cfg.listen)
space = conn.space.test

space:select{1}
space:delete{1}
space:select{1}
-- xxx: update comes through, returns 0 rows affected 
space:update(1, {{'=', 2, 'I am a new tuple'}})
-- nothing is selected, since nothing was there
space:select{1}
space:insert{1, 'I am a new tuple'}
space:select{1}
space:update(1, {{'=', 2, 'I am the newest tuple'}})
space:select{1}
-- this is correct, can append field to tuple
space:update(1, {{'=', 2, 'Huh'}, {'=', 3, 'I am a new field! I was added via append'}})
space:select{1}

-- this is illegal
space:update(1, {{'=', 2, 'Huh'}, {'=', 1001, 'invalid field'}})
space:select{1}
space:replace{1, 'I am a new tuple', 'stub'}
space:update(1, {{'=', 2, 'Huh'}, {'=', 3, 'Oh-ho-ho'}})
space:select{1}


-- check empty strings
space:update(1, {{'=', 2, ''}, {'=', 3, ''}})
space:select{1}

-- check type change 
space:update(1, {{'=', 2, 2}, {'=', 3, 3}})
space:select{1}

-- check limits
space:insert{0}
space:select{0}
space:select{4294967295}

-- cleanup 
space:delete(0)
space:delete(4294967295)
box.space.test:drop()
box.schema.user.drop('test')
space = nil
