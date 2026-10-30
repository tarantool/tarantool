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
-- 32-bit hash select fields test
-------------------------------------------------------------------------------

-- select by valid keys
hash.index['primary']:get{0}
hash.index['primary']:get{1}
hash.index['primary']:get{2}
hash.index['primary']:get{3}
hash.index['primary']:get{4}
hash.index['primary']:get{5}

-- select by invalid keys
hash.index['primary']:get{'invalid key'}
hash.index['primary']:get{1, 2}

hash:drop()
