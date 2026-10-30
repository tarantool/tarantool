hash = box.schema.space.create('tweedledum')
tmp = hash:create_index('primary', { type = 'hash', parts = {1, 'string'}, unique = true })

-- Insert valid fields
hash:insert{'key 0', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 1', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 2', 'value1 v1.0', 'value2 v1.0'}
hash:insert{'key 3', 'value1 v1.0', 'value2 v1.0'}

-------------------------------------------------------------------------------
-- String hash select fields test
-------------------------------------------------------------------------------

-- select by valid keys
hash.index['primary']:get{'key 0'}
hash.index['primary']:get{'key 1'}
hash.index['primary']:get{'key 2'}
hash.index['primary']:get{'key 3'}
hash.index['primary']:get{'key 4'}
hash.index['primary']:get{'key 5'}

-- select by invalid keys
hash.index['primary']:get{'key 1', 'key 2'}

hash:drop()
