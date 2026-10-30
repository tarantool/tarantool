------------------------
-- hash::replace tests
------------------------

hash = box.schema.space.create('tweedledum')
tmp = hash:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
tmp = hash:create_index('field1', { type = 'hash', parts = {2, 'unsigned'}, unique = true })
tmp = hash:create_index('field2', { type = 'hash', parts = {3, 'unsigned'}, unique = true })
tmp = hash:create_index('field3', { type = 'hash', parts = {4, 'unsigned'}, unique = true })

hash:insert{0, 0, 0, 0}
hash:insert{1, 1, 1, 1}
hash:insert{2, 2, 2, 2}

-- OK
hash:replace{1, 1, 1, 1}
hash.index['primary']:get{10}
hash.index['field1']:get{10}
hash.index['field2']:get{10}
hash.index['field3']:get{10}
hash.index['primary']:get{1}
hash.index['field1']:get{1}
hash.index['field2']:get{1}
hash.index['field3']:get{1}

-- OK
hash:insert{10, 10, 10, 10}
hash:delete{10}
hash.index['primary']:get{10}
hash.index['field1']:get{10}
hash.index['field2']:get{10}
hash.index['field3']:get{10}

-- TupleFound (primary key)
hash:insert{1, 10, 10, 10}
hash.index['primary']:get{10}
hash.index['field1']:get{10}
hash.index['field2']:get{10}
hash.index['field3']:get{10}
hash.index['primary']:get{1}

-- TupleNotFound (primary key)
hash:replace{10, 10, 10, 10}
hash.index['primary']:get{10}
hash.index['field1']:get{10}
hash.index['field2']:get{10}
hash.index['field3']:get{10}

-- TupleFound (key --1)
hash:insert{10, 0, 10, 10}
hash.index['primary']:get{10}
hash.index['field1']:get{10}
hash.index['field2']:get{10}
hash.index['field3']:get{10}
hash.index['field1']:get{0}

-- TupleFound (key --1)
-- hash:replace_if_exists(2, 0, 10, 10)
hash.index['primary']:get{10}
hash.index['field1']:get{10}
hash.index['field2']:get{10}
hash.index['field3']:get{10}
hash.index['field1']:get{0}

-- TupleFound (key --3)
hash:insert{10, 10, 10, 0}
hash.index['primary']:get{10}
hash.index['field1']:get{10}
hash.index['field2']:get{10}
hash.index['field3']:get{10}
hash.index['field3']:get{0}

-- TupleFound (key --3)
-- hash:replace_if_exists(2, 10, 10, 0)
hash.index['primary']:get{10}
hash.index['field1']:get{10}
hash.index['field2']:get{10}
hash.index['field3']:get{10}
hash.index['field3']:get{0}

hash:drop()

hash = box.schema.space.create('tweedledum')
hi = hash:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
hash:insert{0}
hash:insert{16}
for _, tuple in hi:pairs(nil, {iterator = box.index.ALL}) do hash:delete{tuple[1]} end
hash:drop()
