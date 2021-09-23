s = box.schema.create_space('test')
_ = s:create_index('pk')
dict = {key = 'value'}
-- There must be errors saying tuple must be array
s:select(dict)
s:pairs(dict)
s:get(dict)
s:drop()
