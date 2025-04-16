errinj = box.error.injection

-- Check a failed realloc in tree index.

s = box.schema.space.create('tweedledum')
index = s:create_index('primary', {type = 'tree'} )

for i = 1,10 do s:insert{i, i, 'test' .. i} end
res = {}
for i = 1,10 do table.insert(res, s:get{i}) end
res
res = {}
for _, t in s.index[0]:pairs() do table.insert(res, t) end
res

errinj.set("ERRINJ_INDEX_ALLOC", true)

res = {}
for i = 1,10 do table.insert(res, s:get{i}) end
res
res = {}
for _, t in s.index[0]:pairs() do table.insert(res, t) end
res

for i = 501,2500 do s:insert{i, i} end
s:delete{1} -- still can delete, it does not require extents if no read view

res = {}
for i = 1,10 do table.insert(res, (s:get{i})) end
res
res = {}
for i = 501,2500 do table.insert(res, (s:get{i})) end
#res -- only could insert some of entries with the extents reserved previously

function check_iter_and_size(num) local count = 0 for _, t in s.index[0]:pairs() do count = count + 1 end return count == num and "ok" or "fail" end
check_iter_and_size(621)

for i = 2501,3500 do s:insert{i, i} end
s:delete{2} -- still can delete, it does not require extents if no read view
check_iter_and_size(620)
res = {}
for i = 1,10 do table.insert(res, (s:get{i})) end
res

errinj.set("ERRINJ_INDEX_ALLOC", false)

for i = 4501,5500 do s:insert{i, i} end
res = {}
for i = 1,10 do table.insert(res, (s:get{i})) end
res
s:delete{8}
res = {}
for i = 1,10 do table.insert(res, (s:get{i})) end
res
res = {}
for i = 5001,5010 do table.insert(res, (s:get{i})) end
res
s:drop()

-----------------------------------

-- Check a failed realloc in hash index.

s = box.schema.space.create('tweedledum')
index = s:create_index('primary', {type = 'hash'} )

for i = 1,10 do s:insert{i, i, 'test' .. i} end
res = {}
for i = 1,10 do table.insert(res, s:get{i}) end
res
res = {}
for _, t in s.index[0]:pairs() do table.insert(res, t) end
res

errinj.set("ERRINJ_INDEX_ALLOC", true)

res = {}
for i = 1,10 do table.insert(res, s:get{i}) end
res
res = {}
for _, t in s.index[0]:pairs() do table.insert(res, t) end
res

for i = 501,2500 do s:insert{i, i} end
s:delete{1} -- still can delete, it does not require extents if no read view

res = {}
for i = 1,10 do table.insert(res, (s:get{i})) end
res
res = {}
for i = 501,2500 do table.insert(res, (s:get{i})) end
#res -- only could insert some of entries with the extents reserved previously

check_iter_and_size(1023)

for i = 2501,3500 do s:insert{i, i} end
s:delete{2} -- still can delete, it does not require extents if no read view
check_iter_and_size(1023)
res = {}
for i = 1,10 do table.insert(res, (s:get{i})) end
res

for i = 3501,4500 do s:insert{i, i} end
s:delete{3} -- still can delete, it does not require extents if no read view
check_iter_and_size(1023)

errinj.set("ERRINJ_INDEX_ALLOC", false)

for i = 4501,5500 do s:insert{i, i} end
res = {}
for i = 1,10 do table.insert(res, (s:get{i})) end
res
s:delete{8}
res = {}
for i = 1,10 do table.insert(res, (s:get{i})) end
res
res = {}
for i = 5001,5010 do table.insert(res, (s:get{i})) end
res

errinj.set("ERRINJ_INDEX_OOM", true)
s:replace{3594, 3594}
errinj.set("ERRINJ_INDEX_OOM", false)

s:drop()

errinj = nil
