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
-- 32-bit hash replace fields tests
-------------------------------------------------------------------------------

-- Replace valid fields
hash:replace{3, 'value1 v1.31', 'value2 1.12'}
hash:replace{1, 'value1 v1.32', 'value2 1.72'}
hash:replace{2, 'value1 v1.43', 'value2 1.92'}

-- Replace invalid fields
hash:replace{'invalid key', 'value1 v1.0', 'value2 v1.0'}

hash:drop()
