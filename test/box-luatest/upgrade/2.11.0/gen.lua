box.cfg{}

-- DDL is disabled until schema is upgraded (see gh-7149) so we have to grant
-- permissions required to run the test manually.
box.schema.user.grant('guest', 'super')

box.schema.create_space('gh_5997')

box.snapshot()

os.exit(0)
