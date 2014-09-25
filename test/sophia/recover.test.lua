
sophia_rmdir()

-- snapshot

space = box.schema.create_space('test', { id = 100, engine = 'sophia' })
space:create_index('primary')
sophia_printdir()
box.snapshot()
space:drop()
box.snapshot()

sophia_rmdir()
