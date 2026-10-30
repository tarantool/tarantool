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

-- Insert invalid fields
hash:insert{'invalid key', 'value1 v1.0', 'value2 v1.0'}

hash:drop()
