space = box.schema.space.create('tweedledum')
i0 = space:create_index('primary', { type = 'tree', parts = {1, 'num'}, unique = true })
i1 = space:create_index('i1', { type = 'tree', parts = {2, 'num'}, unique = false })
i2 = space:create_index('i2', { type = 'tree', parts = {3, 'num'}, unique = false })
i3 = space:create_index('i3', { type = 'tree', parts = {4, 'str', 5, 'str'}, unique = false })
i4 = space:create_index('i4', { type = 'tree', parts = {7, 'str', 6, 'str'}, unique = false })
i5 = space:create_index('i5', { type = 'tree', parts = {9, 'num'}, unique = false })
i6 = space:create_index('i6', { type = 'tree', parts = {7, 'str', 6, 'str', 4, 'str', 5, 'str', 9, 'num'}, unique = true })

space:insert{0, 0, 100, 'Joe', 'Sixpack', 'Drinks', 'Amstel', 'bar', 2000}
space:insert{1, 1, 200, 'Joe', 'Sixpack', 'Drinks', 'Heineken', 'bar', 2001}
space:insert{2, 2, 200, 'Joe', 'Sixpack', 'Drinks', 'Carlsberg', 'bar', 2002}
space:insert{3, 3, 300, 'Joe', 'Sixpack', 'Drinks', 'Corona Extra', 'bar', 2003}
space:insert{4, 4, 300, 'Joe', 'Sixpack', 'Drinks', 'Stella Artois', 'bar', 2004}
space:insert{5, 5, 300, 'Joe', 'Sixpack', 'Drinks', 'Miller Genuine Draft', 'bar', 2005}
space:insert{6, 6, 400, 'John', 'Smoker', 'Hits', 'A Pipe', 'foo', 2006}
space:insert{7, 7, 400, 'John', 'Smoker', 'Hits', 'A Bong', 'foo', 2007}
space:insert{8, 8, 400, 'John', 'Smoker', 'Rolls', 'A Joint', 'foo', 2008}
space:insert{9, 9, 400, 'John', 'Smoker', 'Rolls', 'A Blunt', 'foo', 2009}

-- In non-unique indexes select output order is undefined,
-- so it's better to additionally sort output to receive same order every time.
function sort_cmp(a, b) return a[1] < b[1] and true or false end
function sort(t) table.sort(t, sort_cmp) return t end

space.index['primary']:get{1}
sort(space.index['i1']:select{2})
sort(space.index[2]:select({300}))
#space.index['i3']:select({'Joe', 'Sixpack'})
#space.index['i3']:select('John')
#space.index['i4']:select('A Pipe')
{sort(space.index['i4']:select{'Miller Genuine Draft', 'Drinks'})}
sort(space.index['i5']:select{2007})
sort(space.index[6]:select{'Miller Genuine Draft', 'Drinks'})

space:delete{6}
space:delete{7}
space:delete{8}
space:delete{9}

space:insert{6, 6ULL, 400ULL, 'John', 'Smoker', 'Hits', 'A Pipe', 'foo', 2006}
space:insert{7, 7ULL, 400ULL, 'John', 'Smoker', 'Hits', 'A Bong', 'foo', 2007}
space:insert{8, 8ULL, 400ULL, 'John', 'Smoker', 'Rolls', 'A Joint', 'foo', 2008}
space:insert{9, 9ULL, 400ULL, 'John', 'Smoker', 'Rolls', 'A Blunt', 'foo', 2009}

sort(space.index['i1']:select{6ULL})
sort(space.index['i1']:select{6})
sort(space.index['i2']:select(400ULL))
sort(space.index['i2']:select(400))

sort(space:select{})

-- Test incorrect keys - supplied key field type does not match index type
-- https://bugs.launchpad.net/tarantool/+bug/1072624
space:insert{'', 1, 2, '', '', '', '', '', 0}
space:insert{'xxxxxxxx', 1, 2, '', '', '', '', '', 0}
space:insert{1, '', 2, '', '', '', '', '', 0}
space:insert{1, 'xxxxxxxxxxx', 2, '', '', '', '', '', 0}

space:drop()
sort = nil
sort_cmp = nil
