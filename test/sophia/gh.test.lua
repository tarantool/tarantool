
-- gh-283: Sophia: hang after three creates and drops

s = box.schema.space.create('space0', {engine='sophia'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'STR'}})
s:insert{'a', 'b', 'c'}
s:drop()
sophia_schedule()

s = box.schema.space.create('space0', {engine='sophia'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'STR'}})
s:insert{'a', 'b', 'c'}
t = s.index[0]:select({}, {iterator = box.index.ALL})
t
s:drop()
sophia_schedule()

s = box.schema.space.create('space0', {engine='sophia'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'STR'}})
s:insert{'a', 'b', 'c'}
t = s.index[0]:select({}, {iterator = box.index.ALL})
t
s:drop()
sophia_schedule()

-- gh-280: Sophia: crash if insert without index

s = box.schema.space.create('test', {engine='sophia'})
s:insert{'a'}
s:drop()
sophia_schedule()

-- gh-436: No error when creating temporary sophia space

s = box.schema.space.create('tester',{engine='sophia', temporary=true})

-- gh-432: Sophia: ignored limit

s = box.schema.space.create('tester',{engine='sophia'})
i = s:create_index('sophia_index', {})
for v=1, 100 do s:insert({v}) end
t = s:select({''},{iterator='GT', limit =1})
t
t = s:select({},{iterator='GT', limit =1})
t
s:drop()
sophia_schedule()

s = box.schema.space.create('tester', {engine='sophia'})
i = s:create_index('sophia_index', {type = 'tree', parts = {1, 'STR'}})
for v=1, 100 do s:insert({tostring(v)}) end
t = s:select({''},{iterator='GT', limit =1})
t
t = s:select({},{iterator='GT', limit =1})
t
s:drop()
sophia_schedule()

-- gh-680: Sophia: assertion on update
s = box.schema.space.create('tester', {engine='sophia'})
i = s:create_index('primary',{type = 'tree', parts = {2, 'STR'}})
s:insert{1,'X'}
s:update({'X'}, {{'=', 2, 'Y'}})
s:select{'X'}
s:select{'Y'}
s:update({'X'}, {{'=', 3, 'Z'}})
s:select{'X'}
s:select{'Y'}
s:drop()
sophia_schedule()
