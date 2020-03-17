-------------------------------------------------------------------------------
-- 32-bit hash insert fields tests
-------------------------------------------------------------------------------
hash = box.schema.space.create('tweedledum')
tmp = hash:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })

-- Insert valid fields
hash:insert{0, 'value1 v1.0', 'value2 v1.0'}
hash:insert{1, 'value1 v1.0', 'value2 v1.0'}
hash:insert{2, 'value1 v1.0', 'value2 v1.0'}
hash:insert{3, 'value1 v1.0', 'value2 v1.0'}

-------------------------------------------------------------------------------
-- 32-bit hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
hash:delete{0}
hash:delete{1}
hash:delete{2}
hash:delete{3}
hash:delete{4}
hash:delete{5}

-- delete by invalid keys
hash:delete{'invalid key'}
hash:delete{1, 2}

hash:drop()
