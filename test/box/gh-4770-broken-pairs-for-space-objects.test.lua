--
-- gh-4770: Iteration through space with Lua builtin pairs routine
--
s = box.schema.create_space('test')
-- Check whether __pairs is set for the space object, since Lua Fun
-- handles it manually underneath.
getmetatable(s).__pairs == s.pairs
-- Check whether pairs builtin behaviour doesn't change when the
-- __pairs is set.
keys = {}
for k, v in pairs(s) do keys[k] = true end
keys.name, keys.id, keys.engine
s:drop()
