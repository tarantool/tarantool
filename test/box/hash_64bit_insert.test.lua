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

-- Insert invalid fields
hash:insert{100, 'value1 v1.0', 'value2 v1.0'}
hash:insert{101, 'value1 v1.0', 'value2 v1.0'}
hash:insert{102, 'value1 v1.0', 'value2 v1.0'}
hash:insert{103, 'value1 v1.0', 'value2 v1.0'}
hash:insert{'invalid key', 'value1 v1.0', 'value2 v1.0'}

hash:drop()
