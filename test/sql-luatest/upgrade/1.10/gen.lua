box.cfg{}
box.schema.space.create('T1', {format = {{name = 'x', type = 'unsigned'}}})
box.space.T1:create_index('primary', {parts = {1}})
box.schema.user.grant('guest', 'super')
box.snapshot()
os.exit(0)
