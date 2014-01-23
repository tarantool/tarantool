-- temporary spaces
-- not a temporary
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
s:select{1}
s:len()

box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, 'temporary'}})
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, ''}})

--# stop server default
--# start server default

s = box.space.t
s:len()
s.temporary

box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, 'no-temporary'}})
s.temporary
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, ',:asfda:temporary'}})
s.temporary
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, 'a,b,c,d,e'}})
s.temporary
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, 'temporary'}})
s.temporary

s:select{1}
s:insert{1, 2, 3}

box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, 'temporary'}})
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, 'no-temporary'}})

s:delete{1}
box.space[box.schema.SPACE_ID]:update(s.n, {{'=', 3, 'no-temporary'}})
s:drop()
