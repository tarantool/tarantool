-- temporary spaces
_space = box.space._space
-- not a temporary
FLAGS = 6
s = box.schema.space.create('t', { temporary = true })
s.temporary
s:drop()

-- not a temporary, too
s = box.schema.space.create('t', { temporary = false })
s.temporary
s:drop()

-- not a temporary, too
s = box.schema.space.create('t', { temporary = nil })
s.temporary
s:drop()

s = box.schema.space.create('t', { temporary = true })
index = s:create_index('primary', { type = 'hash' })

s:insert{1, 2, 3}
s:get{1}
s:len()

_ = _space:update(s.id, {{'=', FLAGS, {temporary = true}}})
s.temporary
_ = _space:update(s.id, {{'=', FLAGS, {temporary = false}}})
s.temporary

-- check that temporary space can be modified in read-only mode (gh-1378)
box.cfg{read_only=true}
box.cfg.read_only
s:insert{2, 3, 4}
s:get{2}
s:len()
box.cfg{read_only=false}
box.cfg.read_only

env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')

s = box.space.t
s:len()
s.temporary
s:drop()
s = nil
