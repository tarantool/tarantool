test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

--
-- Not automatic sequence should not be removed by DROP TABLE.
--
box.schema.user.create('user1')
test_space = box.schema.create_space('T', {         \
    engine = engine,                                \
    format = {{'i', 'integer'}}                     \
})
seq = box.schema.sequence.create('S')
ind = test_space:create_index('I', {sequence = 'S'})
box.schema.user.grant('user1', 'write', 'sequence', 'S')
box.execute('DROP TABLE t');
seqs = box.space._sequence:select{}
#seqs == 1 and seqs[1].name == seq.name or seqs
seq:drop()

--
-- Automatic sequence should be removed at DROP TABLE, together
-- with all the grants.
--
box.execute('CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT)')
seqs = box.space._sequence:select{}
#seqs == 1 or seqs
seq = seqs[1].name
box.schema.user.grant('user1', 'write', 'sequence', seq)
box.execute('DROP TABLE t')
seqs = box.space._sequence:select{}
#seqs == 0 or seqs

box.schema.user.drop('user1')
