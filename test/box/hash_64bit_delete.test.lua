-------------------------------------------------------------------------------
-- 64-bit hash insert fields tests
-------------------------------------------------------------------------------
hash = box.schema.space.create('tweedledum')
tmp = hash:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })

-- Insert valid fields
hash:insert{0ULL, 'value1 v1.0', 'value2 v1.0'}
hash:insert{1ULL, 'value1 v1.0', 'value2 v1.0'}
hash:insert{2ULL, 'value1 v1.0', 'value2 v1.0'}
hash:insert{3ULL, 'value1 v1.0', 'value2 v1.0'}

-------------------------------------------------------------------------------
-- 64-bit hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
hash:delete{0ULL}
hash:delete{1ULL}
hash:delete{2ULL}
hash:delete{3ULL}
hash:delete{4ULL}
hash:delete{5ULL}

hash:insert{0ULL, 'value1 v1.0', 'value2 v1.0'}
hash:insert{1ULL, 'value1 v1.0', 'value2 v1.0'}
hash:insert{2ULL, 'value1 v1.0', 'value2 v1.0'}
hash:insert{3ULL, 'value1 v1.0', 'value2 v1.0'}

-- delete by valid NUM keys
hash:delete{0}
hash:delete{1}
hash:delete{2}
hash:delete{3}
hash:delete{4}
hash:delete{5}

-- delete by invalid keys
hash:delete{'invalid key'}
hash:delete{'00000001', '00000002'}

hash:drop()
