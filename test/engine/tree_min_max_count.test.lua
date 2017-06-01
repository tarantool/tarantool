env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
-- one part indices

-- int type

space0 = box.schema.space.create('space0', { engine = engine })
index0 = space0:create_index('primary', { type = 'tree', parts = {1, 'INTEGER'} })

space0:insert({1, "AAAA"})
space0:insert({2, "AAAA"})
space0:insert({3, "AAAA"})
space0:insert({4, "AAAA"})

index0:select()
index0:max(2)
index0:min(2)
index0:count(2)
index0:max()
index0:min()
index0:count()

space0:insert({20, "AAAA"})
space0:insert({30, "AAAA"})
space0:insert({40, "AAAA"})

index0:select()
index0:max(15)
index0:min(15)
index0:count(15)
index0:max()
index0:min()
index0:count()

space0:insert({-2, "AAAA"})
space0:insert({-3, "AAAA"})
space0:insert({-4, "AAAA"})

index0:select()
index0:max(0)
index0:min(0)
index0:count(0)
index0:max()
index0:min()
index0:count()

space0:drop()

-- number type

space1 = box.schema.space.create('space1', { engine = engine })
index1 = space1:create_index('primary', { type = 'tree', parts = {1, 'number'} })

space1:insert({1, "AAAA"})
space1:insert({2, "AAAA"})
space1:insert({3, "AAAA"})
space1:insert({4, "AAAA"})

index1:select()
index1:max(2)
index1:min(2)
index1:count(2)
index1:max()
index1:min()
index1:count()

space1:insert({20, "AAAA"})
space1:insert({30, "AAAA"})
space1:insert({40, "AAAA"})

index1:select()
index1:max(15)
index1:min(15)
index1:count(15)
index1:max()
index1:min()
index1:count()

space1:insert({-2, "AAAA"})
space1:insert({-3, "AAAA"})
space1:insert({-4, "AAAA"})

index1:select()
index1:max(0)
index1:min(0)
index1:count(0)
index1:max()
index1:min()
index1:count()

space1:insert({1.5, "AAAA"})
space1:insert({2.5, "AAAA"})
space1:insert({3.5, "AAAA"})
space1:insert({4.5, "AAAA"})

index1:select()
index1:max(1)
index1:min(1)
index1:count(1)
index1:max()
index1:min()
index1:count()

space1:drop()

-- str type

space2 = box.schema.space.create('space2', { engine = engine })
index2 = space2:create_index('primary', { type = 'tree', parts = {1, 'string'} })
space2:insert({'1', "AAAA"})
space2:insert({'2', "AAAA"})
space2:insert({'3', "AAAA"})
space2:insert({'4', "AAAA"})

index2:select()
index2:max('2')
index2:min('2')
index2:count('2')
index2:max()
index2:min()
index2:count()

space2:insert({'20', "AAAA"})
space2:insert({'30', "AAAA"})
space2:insert({'40', "AAAA"})

index2:select()
index2:max('15')
index2:min('15')
index2:count('15')
index2:max()
index2:min()
index2:count()

space2:insert({'-2', "AAAA"})
space2:insert({'-3', "AAAA"})
space2:insert({'-4', "AAAA"})

index2:select()
index2:max('0')
index2:min('0')
index2:count('0')
index2:max()
index2:min()
index2:count()

space2:drop()

-- num type

space3 = box.schema.space.create('space3', { engine = engine })
index3 = space3:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
space3:insert({1, "AAAA"})
space3:insert({2, "AAAA"})
space3:insert({3, "AAAA"})
space3:insert({4, "AAAA"})

index3:select()
index3:max(2)
index3:min(2)
index3:count(2)
index3:max()
index3:min()
index3:count()

space3:insert({20, "AAAA"})
space3:insert({30, "AAAA"})
space3:insert({40, "AAAA"})

index3:select()
index3:max(15)
index3:min(15)
index3:count(15)
index3:max()
index3:min()
index3:count()

space3:drop()

-- scalar type

space4 = box.schema.space.create('space4', { engine = engine })
index4 = space4:create_index('primary', { type = 'tree', parts = {1, 'scalar'} })
space4:insert({1, "AAAA"})
space4:insert({2, "AAAA"})
space4:insert({3, "AAAA"})
space4:insert({4, "AAAA"})

index4:select()
index4:max(2)
index4:min(2)
index4:count(2)
index4:max()
index4:min()
index4:count()

space4:insert({20, "AAAA"})
space4:insert({30, "AAAA"})
space4:insert({40, "AAAA"})

index4:select()
index4:max(15)
index4:min(15)
index4:count(15)
index4:max()
index4:min()
index4:count()
space4:insert({'1', "AAAA"})
space4:insert({'2', "AAAA"})
space4:insert({'3', "AAAA"})
space4:insert({'4', "AAAA"})

index4:select()
index4:max('2')
index4:min('2')
index4:count('2')
index4:max()
index4:min()
index4:count()

space4:insert({'20', "AAAA"})
space4:insert({'30', "AAAA"})
space4:insert({'40', "AAAA"})

index4:select()
index4:max('15')
index4:min('15')
index4:count('15')
index4:max()
index4:min()
index4:count()

space4:insert({'-2', "AAAA"})
space4:insert({'-3', "AAAA"})
space4:insert({'-4', "AAAA"})

index4:select()
index4:max('0')
index4:min('0')
index4:count('0')
index4:max()
index4:min()
index4:count()

space4:insert({-2, "AAAA"})
space4:insert({-3, "AAAA"})
space4:insert({-4, "AAAA"})

index4:select()
index4:max(0)
index4:min(0)
index4:count(0)
index4:max()
index4:min()
index4:count()

space4:drop()

-- multi filed indices

-- scalar int
space5 = box.schema.space.create('space5', { engine = engine })
index5 = space5:create_index('primary', { type = 'tree', parts = {1, 'scalar', 2, 'INTEGER'} })

space5:insert({1, 1})
space5:insert({1, 2})
space5:insert({1, 3})
space5:insert({1, -4})

index5:select()
index5:max({1})
index5:min({1})
index5:count({1})
index5:max({1, 0})
index5:min({1, 1})
index5:count({1})
index5:max()
index5:min()
index5:count()

space5:insert({2, 1})
space5:insert({2, 2})
space5:insert({2, 3})
space5:insert({2, -4})

index5:select()
index5:max({2})
index5:min({2})
index5:count({2})
index5:max({2, 0})
index5:min({2, 1})
index5:count({2})
index5:max()
index5:min()
index5:count()

space5:drop()

-- scalar str
space6 = box.schema.space.create('space6', { engine = engine })
index6 = space6:create_index('primary', { type = 'tree', parts = {1, 'scalar', 2, 'string'} })

space6:insert({1, '1'})
space6:insert({1, '2'})
space6:insert({1, '3'})
space6:insert({1, '-4'})

index6:select()
index6:max({1})
index6:min({1})
index6:count({1})
index6:max({1, '0'})
index6:min({1, '1'})
index6:count({1})
index6:max()
index6:min()
index6:count()

space6:insert({2, '1'})
space6:insert({2, '2'})
space6:insert({2, '3'})
space6:insert({2, '-4'})

index6:select()
index6:max({2})
index6:min({2})
index6:count({2})
index6:max({2, '0'})
index6:min({2, '1'})
index6:count({2})
index6:max()
index6:min()
index6:count()

space6:drop()

-- min max count after many inserts

string = require('string')

space7 = box.schema.space.create('space7', { engine = engine })
index7 = space7:create_index('primary', { type = 'tree', parts = {1, 'scalar'} })

long_string = string.rep('A', 650)
for i = 1, 1000 do space7:insert({i, long_string}) end

index7:max({100})
index7:max({700})
index7:min({100})
index7:min({700})
index7:count({2})
index7:max()
index7:min()
index7:count()

space7:drop()

space8 = box.schema.space.create('space8', { engine = engine })
index8 = space8:create_index('primary', { type = 'tree', parts = {1, 'scalar', 2, 'INTEGER'} })

for i = 1, 1000 do space8:insert({i % 10, i, long_string}) end

index8:max({1, 100})
index8:max({2, 700})
index8:max({3})
index8:min({1, 10})
index8:min({1, 700})
index8:min({3})
index8:count({2})
index8:max()
index8:min()
index8:count()

space8:drop()
