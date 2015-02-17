##

print('test')

box.cfg{}
require('console').listen(33013)

space = box.schema.create_space('test', { id = 101, engine="sophia" })
index = space:create_index('primary')

for key = 1, 10000000 do
--	box.begin()
	space:replace({key, key})
--	space:delete({key})
--	box.commit()
end
