-- This script can be used only with Tarantool without the fix for gh-9965.
box.cfg{}

local s = box.schema.space.create('test', {engine = 'vinyl'})
s:create_index('pk', {parts = {1, 'unsigned'}})
s:create_index('sk', {parts = {2, 'integer'}})

s:replace{99999999999998, -99999999999998}
box.snapshot()

s:replace{99999999999999, -99999999999999}
box.snapshot()

os.exit(0)
