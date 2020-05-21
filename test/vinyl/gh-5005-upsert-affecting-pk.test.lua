-- Make sure that applying invalid upsert (which modifies PK)
-- after read (so that key populates tuple cache) does not result
-- in crash.
--
s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:insert{1, 1}
s:select()
s:upsert({1}, {{'+', 1, 1}})
s:select()

s:drop()
