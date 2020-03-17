hash = box.schema.space.create('tweedledum')
tmp = hash:create_index('primary', { type = 'hash', parts = {1, 'string'}, unique = true })

-- Insert valid fields
hash:insert{'key 0', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 1', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 2', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 3', 'value1 v1.0', 'value2 v1.0'}

-------------------------------------------------------------------------------
-- String hash replace fields tests
-------------------------------------------------------------------------------

-- Replace valid fields
hash:replace{'key 3', 'value1 v1.31', 'value2 1.12'}
hash:replace{'key 1', 'value1 v1.32', 'value2 1.72'}
hash:replace{'key 2', 'value1 v1.43', 'value2 1.92'}

hash:drop()
