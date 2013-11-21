dofile('utils.lua')

# Tree single-part unique

box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'str')
-- Tree single-part non-unique
box.insert(box.schema.INDEX_ID, 0, 1, 'i1', 'tree', 0, 1, 1, 'str')
-- Tree multi-part unique
box.insert(box.schema.INDEX_ID, 0, 2, 'i2', 'tree', 1, 2, 1, 'str', 2, 'str')
-- Tree multi-part non-unique
box.insert(box.schema.INDEX_ID, 0, 3, 'i3', 'tree', 0, 2, 2, 'str', 3, 'str')
-- Hash single-part unique
box.insert(box.schema.INDEX_ID, 0, 4, 'i4', 'hash', 1, 1, 0, 'str')
-- Hash multi-part unique
box.insert(box.schema.INDEX_ID, 0, 5, 'i5', 'hash', 1, 2, 1, 'str', 2, 'str')

space = box.space[0]

space:insert('pid_001', 'sid_001', 'tid_998', 'a')
space:insert('pid_002', 'sid_001', 'tid_997', 'a')
space:insert('pid_003', 'sid_002', 'tid_997', 'b')
space:insert('pid_005', 'sid_002', 'tid_996', 'b')
space:insert('pid_007', 'sid_003', 'tid_996', 'a')
space:insert('pid_011', 'sid_004', 'tid_996', 'c')
space:insert('pid_013', 'sid_005', 'tid_996', 'b')
space:insert('pid_017', 'sid_006', 'tid_996', 'a')
space:insert('pid_019', 'sid_005', 'tid_995', 'a')
space:insert('pid_023', 'sid_005', 'tid_994', 'a')

-------------------------------------------------------------------------------
-- Iterator: tree single-part unique
-------------------------------------------------------------------------------

iterate(space.n, 0, 0, 1)
iterate(space.n, 0, 0, 1, box.index.ALL)
iterate(space.n, 0, 0, 1, box.index.EQ)
iterate(space.n, 0, 0, 1, box.index.REQ)
iterate(space.n, 0, 0, 1, box.index.GE)
iterate(space.n, 0, 0, 1, box.index.GT)
iterate(space.n, 0, 0, 1, box.index.LE)
iterate(space.n, 0, 0, 1, box.index.LT)
iterate(space.n, 0, 0, 1, box.index.EQ, 'pid_003')
iterate(space.n, 0, 0, 1, box.index.REQ, 'pid_003')
iterate(space.n, 0, 0, 1, box.index.EQ, 'pid_666')
iterate(space.n, 0, 0, 1, box.index.REQ, 'pid_666')
iterate(space.n, 0, 0, 1, box.index.GE, 'pid_001')
iterate(space.n, 0, 0, 1, box.index.GT, 'pid_001')
iterate(space.n, 0, 0, 1, box.index.GE, 'pid_999')
iterate(space.n, 0, 0, 1, box.index.GT, 'pid_999')
iterate(space.n, 0, 0, 1, box.index.LE, 'pid_002')
iterate(space.n, 0, 0, 1, box.index.LT, 'pid_002')
iterate(space.n, 0, 0, 1, box.index.LE, 'pid_000')
iterate(space.n, 0, 0, 1, box.index.LT, 'pid_000')

-------------------------------------------------------------------------------
-- Iterator: tree single-part non-unique
-------------------------------------------------------------------------------

iterate(space.n, 1, 1, 2, box.index.ALL)
iterate(space.n, 1, 1, 2, box.index.EQ)
iterate(space.n, 1, 1, 2, box.index.REQ)
iterate(space.n, 1, 1, 2, box.index.GE)
iterate(space.n, 1, 1, 2, box.index.GT)
iterate(space.n, 1, 1, 2, box.index.LE)
iterate(space.n, 1, 1, 2, box.index.LT)
iterate(space.n, 1, 1, 2, box.index.EQ, 'sid_005')
iterate(space.n, 1, 1, 2, box.index.REQ, 'sid_005')
iterate(space.n, 1, 1, 2, box.index.GE, 'sid_005')
iterate(space.n, 1, 1, 2, box.index.GT, 'sid_005')
iterate(space.n, 1, 1, 2, box.index.GE, 'sid_999')
iterate(space.n, 1, 1, 2, box.index.GT, 'sid_999')
iterate(space.n, 1, 1, 2, box.index.LE, 'sid_005')
iterate(space.n, 1, 1, 2, box.index.LT, 'sid_005')
iterate(space.n, 1, 1, 2, box.index.LE, 'sid_000')
iterate(space.n, 1, 1, 2, box.index.LT, 'sid_000')

-------------------------------------------------------------------------------
-- Iterator: tree multi-part unique
-------------------------------------------------------------------------------

iterate(space.n, 2, 1, 3, box.index.ALL)
iterate(space.n, 2, 1, 3, box.index.EQ)
iterate(space.n, 2, 1, 3, box.index.REQ)
iterate(space.n, 2, 1, 3, box.index.GE)
iterate(space.n, 2, 1, 3, box.index.GT)
iterate(space.n, 2, 1, 3, box.index.LE)
iterate(space.n, 2, 1, 3, box.index.LT)
iterate(space.n, 2, 1, 3, box.index.EQ, 'sid_005')
iterate(space.n, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_995')
iterate(space.n, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_999')
iterate(space.n, 2, 1, 3, box.index.REQ, 'sid_005')
iterate(space.n, 2, 1, 3, box.index.REQ, 'sid_005', 'tid_995')
iterate(space.n, 2, 1, 3, box.index.REQ, 'sid_005', 'tid_999')
iterate(space.n, 2, 1, 3, box.index.GE, 'sid_005')
iterate(space.n, 2, 1, 3, box.index.GT, 'sid_005')
iterate(space.n, 2, 1, 3, box.index.GE, 'sid_005', 'tid_995')
iterate(space.n, 2, 1, 3, box.index.GT, 'sid_005', 'tid_995')
iterate(space.n, 2, 1, 3, box.index.GE, 'sid_005', 'tid_999')
iterate(space.n, 2, 1, 3, box.index.GT, 'sid_005', 'tid_999')
iterate(space.n, 2, 1, 3, box.index.GE, 'sid_999')
iterate(space.n, 2, 1, 3, box.index.GT, 'sid_999')
iterate(space.n, 2, 1, 3, box.index.LE, 'sid_005')
iterate(space.n, 2, 1, 3, box.index.LT, 'sid_005')
iterate(space.n, 2, 1, 3, box.index.LE, 'sid_005', 'tid_997')
iterate(space.n, 2, 1, 3, box.index.LT, 'sid_005', 'tid_997')
iterate(space.n, 2, 1, 3, box.index.LE, 'sid_005', 'tid_000')
iterate(space.n, 2, 1, 3, box.index.LT, 'sid_005', 'tid_000')
iterate(space.n, 2, 1, 3, box.index.LE, 'sid_000')
iterate(space.n, 2, 1, 3, box.index.LT, 'sid_000')

-------------------------------------------------------------------------------
-- Iterator: tree multi-part non-unique
-------------------------------------------------------------------------------

iterate(space.n, 3, 2, 4, box.index.ALL)
iterate(space.n, 3, 2, 4, box.index.EQ)
iterate(space.n, 3, 2, 4, box.index.REQ)
iterate(space.n, 3, 2, 4, box.index.GE)
iterate(space.n, 3, 2, 4, box.index.GT)
iterate(space.n, 3, 2, 4, box.index.LE)
iterate(space.n, 3, 2, 4, box.index.LT)
iterate(space.n, 3, 2, 4, box.index.EQ, 'tid_996')
iterate(space.n, 3, 2, 4, box.index.EQ, 'tid_996', 'a')
iterate(space.n, 3, 2, 4, box.index.EQ, 'tid_996', 'z')
iterate(space.n, 3, 2, 4, box.index.REQ, 'tid_996')
iterate(space.n, 3, 2, 4, box.index.REQ, 'tid_996', 'a')
iterate(space.n, 3, 2, 4, box.index.REQ, 'tid_996', '0')
iterate(space.n, 3, 2, 4, box.index.GE, 'tid_997')
iterate(space.n, 3, 2, 4, box.index.GT, 'tid_997')
iterate(space.n, 3, 2, 4, box.index.GE, 'tid_998')
iterate(space.n, 3, 2, 4, box.index.GT, 'tid_998')
iterate(space.n, 3, 2, 4, box.index.LE, 'tid_997')
iterate(space.n, 3, 2, 4, box.index.LT, 'tid_997')
iterate(space.n, 3, 2, 4, box.index.LE, 'tid_000')
iterate(space.n, 3, 2, 4, box.index.LT, 'tid_000')
iterate(space.n, 3, 2, 4, box.index.LT, 'tid_996', 'to', 'many', 'keys')

-------------------------------------------------------------------------------
-- Iterator: hash single-part unique
-------------------------------------------------------------------------------

iterate(space.n, 4, 0, 1)
iterate(space.n, 4, 0, 1, box.index.ALL)
iterate(space.n, 4, 0, 1, box.index.EQ)
iterate(space.n, 4, 0, 1, box.index.GE)
iterate(space.n, 4, 0, 1, box.index.EQ, 'pid_003')
iterate(space.n, 4, 0, 1, box.index.EQ, 'pid_666')
iterate(space.n, 4, 0, 1, box.index.GE, 'pid_001')
iterate(space.n, 4, 0, 1, box.index.GE, 'pid_999')

-------------------------------------------------------------------------------
-- Iterator: hash multi-part unique
-------------------------------------------------------------------------------

iterate(space.n, 5, 1, 3, box.index.ALL)
iterate(space.n, 5, 1, 3, box.index.EQ)
iterate(space.n, 5, 1, 3, box.index.EQ, 'sid_005')
iterate(space.n, 5, 1, 3, box.index.GE)
iterate(space.n, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_995')
iterate(space.n, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_999')
iterate(space.n, 2, 1, 3, box.index.EQ, 'sid_005', 'tid_995', 'a')
iterate(space.n, 2, 1, 3, box.index.GE, 'sid_005', 'tid_995')
iterate(space.n, 2, 1, 3, box.index.GE, 'sid_005', 'tid_999')

-------------------------------------------------------------------------------
-- Iterator: various
-------------------------------------------------------------------------------

space.index[0]:iterator(-666)
-- Test cases for #123: box.index.count does not check arguments properly
space.index[0]:iterator(box.index.EQ, function() end)
space:drop()
