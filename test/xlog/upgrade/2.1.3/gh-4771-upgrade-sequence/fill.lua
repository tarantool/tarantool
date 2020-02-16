box.cfg{}
s1 = box.schema.create_space('test1')
pk = s1:create_index('pk', {sequence = true})
s1:replace{box.NULL}

seq2 = box.schema.sequence.create('seq2')
s2 = box.schema.create_space('test2')
pk = s2:create_index('pk', {sequence = 'seq2'})
s2:replace{box.NULL}

seq3 = box.schema.sequence.create('seq3')
seq3:next()

box.snapshot()
