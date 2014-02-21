s = box.schema.create_space('tweedledum')
s:create_index('primary')

-- Check a failed realloc in tree.

for i = 1,10 do s:insert{i, i, 'test' .. i} end
res = {}
for i = 1,10 do table.insert(res, s:select{i}) end
res
res = {}
for t in s.index[0]:iterator() do table.insert(res, t) end
res

box.errinj.set("ERRINJ_TREE_ALLOC", true)

res = {}
for i = 1,10 do table.insert(res, s:select{i}) end
res
for i = 501,1000 do s:insert{i, i} end
s:delete{1}
res = {}
for t in s.index[0]:iterator() do table.insert(res, t) end
res

-- reserve memory for iterator in index. last insert may increase tree depth
box.errinj.set("ERRINJ_TREE_ALLOC", false)
s:select{1}
box.errinj.set("ERRINJ_TREE_ALLOC", true)

res = {}
for i = 1,10 do table.insert(res, (s:select{i})) end
res

for i = 1001,1500 do s:insert{i, i} end
s:delete{2}
s.index[0]:iterator()

-- reserve memory for iterator in index. last insert may increase tree depth
-- (if rebalance was not initiated)
box.errinj.set("ERRINJ_TREE_ALLOC", false)
s:select{1}
box.errinj.set("ERRINJ_TREE_ALLOC", true)

res = {}
for i = 1,10 do table.insert(res, (s:select{i})) end
res
for i = 1501,2000 do s:insert{i, i} end
s:delete{3}
s.index[0]:iterator()

box.errinj.set("ERRINJ_TREE_ALLOC", false)

for i = 2001,2500 do s:insert{i, i} end
res = {}
for i = 1,10 do table.insert(res, (s:select{i})) end
res
s:delete{8}
res = {}
for i = 1,10 do table.insert(res, (s:select{i})) end
res
res = {}
for i = 2001,2010 do table.insert(res, (s:select{i})) end
res
s:drop()

