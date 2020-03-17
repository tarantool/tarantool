hash = box.schema.space.create('tweedledum')
hi = hash:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
hash:insert{0}
hash:insert{16}
for _, tuple in hi:pairs(nil, {iterator = box.index.ALL}) do hash:delete{tuple[1]} end
hash:drop()
