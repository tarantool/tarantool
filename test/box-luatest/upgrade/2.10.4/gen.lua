box.cfg{}

-- DDL is disabled until schema is upgraded (see gh-7149) so we have to grant
-- permissions required to run the test manually.
box.schema.user.grant('guest', 'super')

box.schema.user.create('eve')
box.schema.user.create('bob', {password = 'secret'})
box.schema.role.create('test')

box.schema.func.create('test')
box.schema.sequence.create('test')
box.schema.create_space('test')
box.space.test:create_index('pk')

box.schema.create_space('gh_7858')
box.space.gh_7858:create_index('pk', {
    parts = {{'[2][3]', 'unsigned'}},
    sequence = true,
})

box.snapshot()

os.exit(0)
