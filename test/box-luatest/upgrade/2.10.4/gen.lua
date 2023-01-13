box.cfg{}

box.schema.user.create('eve')
box.schema.user.create('bob', {password = 'secret'})
box.schema.role.create('test')

box.schema.create_space('gh_7858')
box.space.gh_7858:create_index('pk', {
    parts = {{'[2][3]', 'unsigned'}},
    sequence = true,
})

box.snapshot()

os.exit(0)
