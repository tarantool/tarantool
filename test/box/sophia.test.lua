os.execute("rm -rf sophia")

space = box.schema.create_space('tweedledum', { id = 123, engine = 'sophia' })
space:create_index('primary', { type = 'tree', parts = {1, 'num'} })

for v=1, 10 do space:insert({v}) end

t = space.index[0]:select({}, {iterator = box.index.ALL})
t

t = space.index[0]:select({}, {iterator = box.index.GE})
t

t = space.index[0]:select(4, {iterator = box.index.GE})
t

t = space.index[0]:select({}, {iterator = box.index.LE})
t

t = space.index[0]:select(7, {iterator = box.index.LE})
t

t = {}
for v=1, 10 do table.insert(t, space:get({v})) end
t

space:drop()
box.snapshot()

--
-- gh-283: Sophia: hang after three creates and drops
--

s = box.schema.create_space('space0', {id = 33, engine='sophia'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'STR'}})
s:insert{'a', 'b', 'c'}
s:drop()

s = box.schema.create_space('space0', {id = 33, engine='sophia'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'STR'}})
s:insert{'a', 'b', 'c'}
t = s.index[0]:select({}, {iterator = box.index.ALL})
t
s:drop()

s = box.schema.create_space('space0', {id = 33, engine='sophia'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'STR'}})
s:insert{'a', 'b', 'c'}
t = s.index[0]:select({}, {iterator = box.index.ALL})
t
s:drop()

--
-- gh-280: Sophia: crash if insert without index
--

s = box.schema.create_space('test', {engine='sophia'})
s:insert{'a'}
s:drop()

---
--- gh-431: Sophia: assertion if box.begin
---

box.cfg{}
s = box.schema.create_space('tester',{engine='sophia'})
s:create_index('sophia_index', {})
s:insert{10000, 'Hilton'}
box.begin()
s:delete{10000} -- exception
box.rollback()
s:select{10000}
s:drop()

---
--- gh-456: Sophia: index size() is unsupported
---

box.cfg{}
s = box.schema.create_space('tester',{engine='sophia'})
s:create_index('sophia_index', {})
s.index[0]:len() -- exception
box.error()
s:drop()

os.execute("rm -rf sophia")
