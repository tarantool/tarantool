box.cfg.wal_mode
space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })
space:insert{1}
space:insert{2}
space:insert{3}
space.index['primary']:select(1)
space.index['primary']:select(2)
space.index['primary']:select(3)
space.index['primary']:select(4)
box.snapshot()
box.snapshot()
space:truncate()
box.snapshot()
space:drop()
