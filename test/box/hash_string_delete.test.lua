hash = box.schema.space.create('tweedledum')
tmp = hash:create_index('primary', { type = 'hash', parts = {1, 'string'}, unique = true })

-- Insert valid fields
hash:insert{'key 0', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 1', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 2', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 3', 'value1 v1.0', 'value2 v1.0'}

-------------------------------------------------------------------------------
-- String hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
hash:delete{'key 0'}
hash:delete{'key 1'}
hash:delete{'key 2'}
hash:delete{'key 3'}
hash:delete{'key 4'}
hash:delete{'key 5'}

-- delete by invalid keys
hash:delete{'key 1', 'key 2'}
hash:drop()
