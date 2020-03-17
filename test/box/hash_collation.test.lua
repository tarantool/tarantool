-------------------------------------------------------------------------------
-- Collation test
-------------------------------------------------------------------------------

hash = box.schema.space.create('tweedledum')
tmp = hash:create_index('primary', { type = 'hash', parts = {{1, 'string', collation = 'unicode_ci'}}, unique = true})
tmp = hash:create_index('secondary', { type = 'hash', parts = {{2, 'scalar', collation = 'unicode_ci'}}, unique = true})

hash:insert{'Ёж', 'Hedgehog'}
hash:insert{'Ёлка', 'Spruce'}
hash:insert{'Jogurt', 'Йогурт'}
hash:insert{'Один', 1}

hash.index.primary:get('ёж')
hash.index.primary:get('елка')
hash.index.secondary:get('spruce')
hash.index.secondary:get('йогурт')
hash.index.secondary:get(1)
hash.index.secondary:get('иогурт')
hash.index.secondary:get(2)

hash:drop()
