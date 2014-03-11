-- temporary spaces
-- not a temporary
FLAGS = 4
s = box.schema.create_space('t', { temporary = true })
s.temporary
s:drop()

-- not a temporary, too
s = box.schema.create_space('t', { temporary = false })
s.temporary
s:drop()

-- not a temporary, too
s = box.schema.create_space('t', { temporary = nil })
s.temporary
s:drop()

s = box.schema.create_space('t', { temporary = true })
s:create_index('primary', { type = 'hash' })

s:insert{1, 2, 3}
s:get{1}
s:len()

box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, 'temporary'}})
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, ''}})

--# stop server default
--# start server default
FLAGS = 4

s = box.space.t
s:len()
s.temporary

box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, 'no-temporary'}})
s.temporary
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, ',:asfda:temporary'}})
s.temporary
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, 'a,b,c,d,e'}})
s.temporary
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, 'temporary'}})
s.temporary

s:get{1}
s:insert{1, 2, 3}

box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, 'temporary'}})
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, 'no-temporary'}})

s:delete{1}
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', FLAGS, 'no-temporary'}})
s:drop()
