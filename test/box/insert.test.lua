-- gh-186 New implementation of box.replace does not check that tuple is
-- array
s = box.schema.create_space('s')
s:create_index('pk')
s:insert(1)
s:insert(1, 2)
s:insert(1, 2, 3)
s:insert{1}
s:insert{2, 3}
-- xxx: silently truncates the tail - should warn perhaps
s:delete(1, 2, 3)
s:drop()
