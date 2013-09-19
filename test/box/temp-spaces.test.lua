-- temporary spaces

-- not a temporary
s = box.schema.create_space('t', { if_not_exists = true, properties = 'temporarytrash' })
s.is_temporary
s:drop()

-- not a temporary, too
s = box.schema.create_space('t', { if_not_exists = true, properties = 'temporar' })
s.is_temporary
s:drop()

-- not a temporary, too
s = box.schema.create_space('t', { if_not_exists = true, properties = '' })
s.is_temporary
s:drop()

-- not a temporary, too
s = box.schema.create_space('t', { if_not_exists = true, properties = {'a'} })
s.is_temporary
s:drop()

-- temporary
s = box.schema.create_space('t', { if_not_exists = true, properties = {'temporary'} })
s.is_temporary
s:drop()

-- temporary
s = box.schema.create_space('t', { if_not_exists = true, properties = 'temporary' })
s.is_temporary
s:create_index('primary', 'hash', { unique = true, parts = {0, 'num'}})

s:insert(1,2,3)
s:select(0, 1)
s:len()

box.space[box.schema.SPACE_ID]:update(s.n, '=p', 3, 'temporary')
box.space[box.schema.SPACE_ID]:update(s.n, '=p', 3, 'no-temporary')

--# stop server default
--# start server default
s = box.space.t
s:len()
s.is_temporary

box.space[box.schema.SPACE_ID]:update(s.n, '=p', 3, 'no-temporary')
box.space[box.schema.SPACE_ID]:update(s.n, '=p', 3, 'temporary')

s:select(0, 1)
s:insert(1,2,3)

box.space[box.schema.SPACE_ID]:update(s.n, '=p', 3, 'temporary')
box.space[box.schema.SPACE_ID]:update(s.n, '=p', 3, 'no-temporary')


s:delete(1)
box.space[box.schema.SPACE_ID]:update(s.n, '=p', 3, 'no-temporary')
s:drop()

