box.cfg{}
local s1 = box.schema.create_space('test1')
s1:create_index('pk', {sequence = true})
s1:replace{box.NULL}

box.schema.sequence.create('seq2')
local s2 = box.schema.create_space('test2')
s2:create_index('pk', {sequence = 'seq2'})
s2:replace{box.NULL}

local seq3 = box.schema.sequence.create('seq3')
seq3:next()

box.snapshot()
